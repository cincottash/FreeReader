#include "OfflineDictionary.h"

#include "core/practice/Answer.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimer>
#include <QUuid>
#include <QVariant>

#include <algorithm>

namespace core {

namespace {

struct Entry {
    QString word, pos, tags;
    QStringList glosses;
    int tier = 2;
    int rank = 0;
};

// How closely a dictionary spelling matches what was selected: 0 exactly, 1 in other capitals ("El" -> "el"),
// 2 only without accents ("tenia" for "tenía"). A closer match always wins: "qué" is never "que".
int tierOf(const QString& found, const QString& query)
{
    if (found == query) return 0;
    if (found.toLower() == query.toLower()) return 1;
    return 2;
}

bool isLetterName(const Entry& e)
{
    static const QRegularExpression re(QStringLiteral("^(The )?name of the .*letter"), QRegularExpression::CaseInsensitiveOption);
    for (const QString& g : e.glosses)
        if (!re.match(g).hasMatch()) return false;
    return true;
}

QList<Entry> entriesFor(const QSqlDatabase& db, const QString& word, int maxTier = 2)
{
    QList<Entry> out;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT word, pos, tags, glosses FROM entries WHERE folded=?"));
    q.addBindValue(practice::foldAccents(word.toLower()));
    if (!q.exec()) return out;
    while (q.next()) {
        Entry e{q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), {}};
        e.tier = tierOf(e.word, word);
        if (e.tier > maxTier) continue;
        for (const QJsonValue& v : QJsonDocument::fromJson(q.value(3).toString().toUtf8()).array()) e.glosses << v.toString();
        if (e.glosses.isEmpty()) continue;
        // Within a tier: the letter's name ("de": the letter D) and relational adjectives ("astillero":
        // boatmaking) after the word's usual meanings.
        e.rank = e.tier * 10 + (isLetterName(e) ? 5 : 0) + (e.tags.split(QLatin1Char(' ')).contains(QStringLiteral("relational")) ? 3 : 0);
        out << e;
    }
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.rank < b.rank; });
    return out;
}

// "foot (a part of the body)" -> "foot"; keeps short glosses whole. A proper noun keeps its explanation
// ("Manche (a department of Normandy, France)").
QString shortGloss(const QString& g, const QString& pos = QString())
{
    if (pos == QLatin1String("name")) {
        QString s = g.trimmed();
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
        return s;
    }
    QString s = g;
    s.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)")));
    s = s.trimmed();
    // What a removed explanation leaves behind: "to bottle, (wine)" -> "to bottle".
    while (!s.isEmpty() && QStringLiteral(".,;:").contains(s.back())) s.chop(1);
    s = s.trimmed();
    return s.isEmpty() ? g.trimmed() : s;
}

QString posName(const QString& pos)
{
    static const QHash<QString, QString> names = {{"adj", "adjective"}, {"adv", "adverb"}, {"prep", "preposition"}, {"conj", "conjunction"},
                                                  {"intj", "interjection"}, {"pron", "pronoun"}, {"det", "determiner"}, {"num", "numeral"},
                                                  {"name", "proper noun"}, {"phrase", "phrase"}, {"prep_phrase", "phrase"}};
    return names.value(pos, pos);
}

}  // namespace

OfflineDictionary::OfflineDictionary(QObject* parent) : TranslationProvider(parent) {}

OfflineDictionary::~OfflineDictionary() { close(); }

void OfflineDictionary::close()
{
    if (m_connection.isEmpty()) return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        if (db.isOpen()) db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
    m_connection.clear();
    m_info = {};
    m_path.clear();
}

DictionaryInfo OfflineDictionary::inspect(const QString& path)
{
    OfflineDictionary d;
    d.open(path);
    return d.info();
}

bool OfflineDictionary::open(const QString& path)
{
    close();
    m_info = {};
    if (!QFileInfo(path).isFile()) {
        m_info.error = tr("The file doesn't exist.");
        return false;
    }
    m_connection = QStringLiteral("dictionary-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(path);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db.open()) {
        m_info.error = tr("It can't be opened.");
        return false;
    }
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT key, value FROM meta"))) {
        m_info.error = tr("It isn't a FreeReader dictionary.");
        return false;
    }
    while (q.next()) {
        const QString k = q.value(0).toString(), v = q.value(1).toString();
        if (k == QLatin1String("language")) m_info.language = v;
        else if (k == QLatin1String("target")) m_info.target = v;
        else if (k == QLatin1String("source")) m_info.source = v;
        else if (k == QLatin1String("license")) m_info.license = v;
        else if (k == QLatin1String("built")) m_info.built = v;
    }
    if (m_info.language.isEmpty() || !q.exec(QStringLiteral("SELECT COUNT(*) FROM entries")) || !q.next()) {
        m_info.error = tr("It isn't a FreeReader dictionary.");
        return false;
    }
    m_info.entries = q.value(0).toInt();
    m_info.ok = true;
    m_path = path;
    return true;
}

TranslationResult OfflineDictionary::lookUp(const QString& text, const QString& language) const
{
    TranslationResult r;
    r.text = text.trimmed();
    if (!m_info.ok || r.text.isEmpty()) return r;
    if (!language.isEmpty() && !m_info.language.isEmpty() && language != m_info.language) return r;
    const QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    QString word = r.text;
    // Punctuation around a selected word ("¿Qué?", "«hidalgo»,").
    word.remove(QRegularExpression(QStringLiteral("^[\\p{P}\\s]+|[\\p{P}\\s]+$")));
    if (word.isEmpty()) return r;

    const QList<Entry> own = entriesFor(db, word);
    // An inflected form: its description and its dictionary form (the closest spelling match).
    QString lemma, formDescription, formPos;
    int formTier = 99;
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT form, lemma, pos, description FROM forms WHERE folded=?"));
        q.addBindValue(practice::foldAccents(word.toLower()));
        if (q.exec())
            while (q.next()) {
                const int t = tierOf(q.value(0).toString(), word);
                if (t >= formTier) continue;
                formTier = t;
                lemma = q.value(1).toString();
                formPos = q.value(2).toString();
                formDescription = q.value(3).toString();
            }
    }
    const int ownTier = own.isEmpty() ? 99 : std::min_element(own.begin(), own.end(), [](const Entry& a, const Entry& b) { return a.tier < b.tier; })->tier;
    // The closer spelling decides: "tenía" is a form of "tener", not "tenia" (tapeworm) without its accent. At the
    // same closeness the word in its own right wins ("casa" is a house before it is a form of "casar").
    const bool asForm = !lemma.isEmpty() && formTier < ownTier;
    QList<Entry> entries;
    if (asForm) {
        entries = entriesFor(db, lemma, 1);
        if (entries.isEmpty()) entries = entriesFor(db, lemma);
    } else {
        // The closest spellings only ("qué", never also "que"); other capitals follow an exact match as more
        // meanings ("Mancha", the place, then "mancha", a stain).
        for (const Entry& e : own)
            if (e.tier == ownTier || (ownTier == 0 && e.tier == 1)) entries << e;
        lemma.clear();
        formDescription.clear();
    }
    if (entries.isEmpty()) return r;

    const Entry& first = entries.first();
    QStringList meanings;
    for (const Entry& e : entries)
        for (const QString& g : e.glosses) {
            const QString s = shortGloss(g, e.pos);
            if (!s.isEmpty() && !meanings.contains(s, Qt::CaseInsensitive)) meanings << s;
        }
    r.translation = meanings.value(0);
    for (int i = 1; i < meanings.size() && r.alternatives.size() < 4; ++i)
        if (meanings[i].size() <= 40) r.alternatives << meanings[i];
    QStringList pos{posName(first.pos)};
    for (const QString& tag : {QStringLiteral("masculine"), QStringLiteral("feminine")})
        if (first.tags.split(QLatin1Char(' ')).contains(tag)) pos << tag;
    if (!formDescription.isEmpty()) {
        // "first-person singular preterite indicative of piar" -> "first-person singular preterite indicative"
        QString d = formDescription;
        const int of = int(d.lastIndexOf(QStringLiteral(" of ")));
        if (of > 0) d = d.left(of);
        pos = QStringList{posName(formPos.isEmpty() ? first.pos : formPos), d};
    }
    r.partOfSpeech = pos.join(QStringLiteral(", "));
    r.lemma = lemma.compare(r.text, Qt::CaseInsensitive) == 0 ? QString() : lemma;
    r.provider = tr("Offline dictionary");
    return r;
}

void OfflineDictionary::translate(const TranslationRequest& request, Done done)
{
    // A dictionary has words, not passages.
    const TranslationResult r = isSetUp() && request.text.simplified().split(QLatin1Char(' ')).size() <= 4 ? lookUp(request.text, request.from) : TranslationResult();
    const TranslationFailure f = !isSetUp() ? TranslationFailure::NotSetUp : r.translation.isEmpty() ? TranslationFailure::NotFound : TranslationFailure::None;
    QTimer::singleShot(0, this, [done, r, f] { done(r, f, {}); });
}

}  // namespace core

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

namespace core {

namespace {

struct Entry {
    QString word, pos, tags;
    QStringList glosses;
};

QList<Entry> entriesFor(const QSqlDatabase& db, const QString& word)
{
    QList<Entry> out;
    QSqlQuery q(db);
    // Exact spelling first ("Pie" before "pie"), then any case and accents.
    q.prepare(QStringLiteral("SELECT word, pos, tags, glosses FROM entries WHERE word=? UNION ALL "
                             "SELECT word, pos, tags, glosses FROM entries WHERE folded=? AND word<>?"));
    q.addBindValue(word);
    q.addBindValue(practice::foldAccents(word.toLower()));
    q.addBindValue(word);
    if (!q.exec()) return out;
    while (q.next()) {
        Entry e{q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), {}};
        for (const QJsonValue& v : QJsonDocument::fromJson(q.value(3).toString().toUtf8()).array()) e.glosses << v.toString();
        if (!e.glosses.isEmpty()) out << e;
    }
    return out;
}

// "foot (a part of the body)" -> "foot"; keeps short glosses whole.
QString shortGloss(const QString& g)
{
    QString s = g;
    s.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)")));
    s = s.trimmed();
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
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

    QList<Entry> own = entriesFor(db, word);
    // An inflected form: its description and its dictionary form.
    QString lemma, formDescription, formPos;
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT lemma, pos, description FROM forms WHERE form=? UNION ALL SELECT lemma, pos, description FROM forms WHERE folded=? AND form<>?"));
        q.addBindValue(word);
        q.addBindValue(practice::foldAccents(word.toLower()));
        q.addBindValue(word);
        if (q.exec() && q.next()) {
            lemma = q.value(0).toString();
            formPos = q.value(1).toString();
            formDescription = q.value(2).toString();
        }
    }
    QList<Entry> entries = own;
    if (entries.isEmpty() && !lemma.isEmpty()) entries = entriesFor(db, lemma);
    else if (!lemma.isEmpty() && !own.isEmpty()) lemma.clear();   // a word in its own right ("pie" is a noun, not only a form of "piar")
    if (entries.isEmpty()) return r;

    const Entry& first = entries.first();
    QStringList meanings;
    for (const Entry& e : entries)
        for (const QString& g : e.glosses) {
            const QString s = shortGloss(g);
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

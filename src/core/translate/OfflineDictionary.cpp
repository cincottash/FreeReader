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
    QString card;   // the whole entry (JSON), when asked for and the file has it
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

QList<Entry> entriesFor(const QSqlDatabase& db, const QString& word, int maxTier = 2, bool withCard = false)
{
    QList<Entry> out;
    QSqlQuery q(db);
    q.prepare(withCard ? QStringLiteral("SELECT word, pos, tags, glosses, card FROM entries WHERE folded=?")
                       : QStringLiteral("SELECT word, pos, tags, glosses FROM entries WHERE folded=?"));
    q.addBindValue(practice::foldAccents(word.toLower()));
    if (!q.exec()) return out;
    while (q.next()) {
        Entry e{q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), {}};
        e.tier = tierOf(e.word, word);
        if (e.tier > maxTier) continue;
        if (withCard) e.card = q.value(4).toString();
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

struct RegularForm {
    QString lemma, pos, description;
};

// English regular inflections (WordNet's "morphy" rules): the first ending whose removal leaves a word of the
// matching part of speech; a doubled last letter is undone too ("running" -> "run", "bigger" -> "big").
RegularForm regularEnglishForm(const QSqlDatabase& db, const QString& word)
{
    struct Rule {
        const char* pos;
        const char* ending;
        const char* replace;
        const char* description;
        bool undoDouble;
    };
    static const Rule rules[] = {
        {"noun", "ies", "y", "plural", false},      {"noun", "ches", "ch", "plural", false}, {"noun", "shes", "sh", "plural", false},
        {"noun", "ses", "s", "plural", false},      {"noun", "xes", "x", "plural", false},   {"noun", "zes", "z", "plural", false},
        {"noun", "men", "man", "plural", false},    {"noun", "s", "", "plural", false},
        {"verb", "ies", "y", "third-person singular", false}, {"verb", "es", "e", "third-person singular", false},
        {"verb", "es", "", "third-person singular", false},   {"verb", "s", "", "third-person singular", false},
        {"verb", "ied", "y", "past tense or past participle", false}, {"verb", "ed", "e", "past tense or past participle", false},
        {"verb", "ed", "", "past tense or past participle", true},    {"verb", "ing", "e", "present participle", false},
        {"verb", "ing", "", "present participle", true},
        {"adj", "ier", "y", "comparative", false},  {"adj", "er", "e", "comparative", false},  {"adj", "er", "", "comparative", true},
        {"adj", "iest", "y", "superlative", false}, {"adj", "est", "e", "superlative", false}, {"adj", "est", "", "superlative", true},
    };
    auto has = [&](const QString& candidate, const QString& pos) {
        if (candidate.size() < 2) return false;
        for (const Entry& e : entriesFor(db, candidate, 1))
            if (e.pos == pos) return true;
        return false;
    };
    for (const Rule& r : rules) {
        const QString ending = QString::fromLatin1(r.ending);
        if (!word.endsWith(ending) || word.size() <= ending.size() + 1) continue;
        const QString stem = word.left(word.size() - ending.size());
        const QString pos = QString::fromLatin1(r.pos);
        QString candidate = stem + QString::fromLatin1(r.replace);
        if (has(candidate, pos)) return {candidate, pos, QString::fromLatin1(r.description)};
        if (r.undoDouble && stem.size() >= 3 && stem.back() == stem.at(stem.size() - 2) && !QStringLiteral("aeiou").contains(stem.back())) {
            candidate = stem.left(stem.size() - 1);
            if (has(candidate, pos)) return {candidate, pos, QString::fromLatin1(r.description)};
        }
    }
    return {};
}

// What an English form is, from its ending, for forms WordNet lists without saying ("ran", "geese").
QString englishFormName(const QString& form, const QString& pos)
{
    if (pos == QLatin1String("verb")) {
        if (form.endsWith(QLatin1String("ing"))) return QStringLiteral("present participle");
        if (form.endsWith(QLatin1String("s")) && !form.endsWith(QLatin1String("ss"))) return QStringLiteral("third-person singular");
        return QStringLiteral("past tense or past participle");
    }
    if (pos == QLatin1String("noun")) return QStringLiteral("plural");
    if (pos == QLatin1String("adj") || pos == QLatin1String("adv")) {
        if (form.endsWith(QLatin1String("est"))) return QStringLiteral("superlative");
        if (form.endsWith(QLatin1String("er"))) return QStringLiteral("comparative");
    }
    return {};
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
        else if (k == QLatin1String("title")) m_info.title = v;
    }
    m_info.cards = q.exec(QStringLiteral("SELECT card FROM entries LIMIT 1"));
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
    // English possessives: "Shakespeare's" is Shakespeare (unless the dictionary has the whole word: "o'clock").
    if (m_info.language == QLatin1String("en") && word.size() > 3 &&
        (word.endsWith(QLatin1String("'s"), Qt::CaseInsensitive) || word.endsWith(QStringLiteral("’s"), Qt::CaseInsensitive)) &&
        entriesFor(db, word, 1).isEmpty())
        word.chop(2);

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
    // English: WordNet lists only irregular forms ("ran", "geese"); regular ones ("books", "walked", "running",
    // "bigger") are found by taking their ending off, as WordNet's own "morphy" does.
    RegularForm also;
    if (m_info.language == QLatin1String("en")) {
        // A listed irregular form ("ran", "running"), else a regular one ("walked").
        const RegularForm f = !lemma.isEmpty() ? RegularForm{lemma, formPos, formDescription.isEmpty() ? englishFormName(word.toLower(), formPos) : formDescription}
                                               : regularEnglishForm(db, word.toLower());
        const int fTier = !lemma.isEmpty() ? formTier : 1;
        lemma.clear();
        if (!f.lemma.isEmpty()) {
            // A word that is also an entry of its own ("flies": a theatre's fly space) is usually the form of a
            // commoner word: the one with more meanings wins.
            auto senses = [](const QList<Entry>& list) {
                qsizetype n = 0;
                for (const Entry& e : list)
                    if (e.tier <= 1) n += e.glosses.size();
                return n;
            };
            if (ownTier > fTier || senses(entriesFor(db, f.lemma, 1)) >= senses(own)) {
                lemma = f.lemma;
                formPos = f.pos;
                formDescription = f.description;
                formTier = qMin(fTier, ownTier - 1);
            } else {
                // The word's own meaning first ("a running joke"), the verb it comes from after it ("run").
                also = f;
            }
        }
    }
    // The closer spelling decides: "tenía" is a form of "tener", not "tenia" (tapeworm) without its accent. At the
    // same closeness the word in its own right wins ("casa" is a house before it is a form of "casar").
    const bool asForm = !lemma.isEmpty() && formTier < ownTier;
    QList<Entry> entries;
    if (asForm) {
        entries = entriesFor(db, lemma, 1);
        if (entries.isEmpty()) entries = entriesFor(db, lemma);
        // The form's part of speech first ("hopped": the verb "hop", before the noun).
        if (!formPos.isEmpty())
            std::stable_partition(entries.begin(), entries.end(), [&](const Entry& e) { return e.pos == formPos; });
    } else {
        // The closest spellings only ("qué", never also "que"); other capitals follow an exact match as more
        // meanings ("Mancha", the place, then "mancha", a stain).
        for (const Entry& e : own)
            if (e.tier == ownTier || (ownTier == 0 && e.tier == 1)) entries << e;
        lemma.clear();
        formDescription.clear();
    }
    if (entries.isEmpty()) return r;
    // WordNet lists parts of speech in no useful order: the one with the most meanings (the commonest) first
    // ("colour": the noun, not the rare adjective). Wiktionary's own order is kept.
    if (isMonolingual() && !asForm)
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.tier != b.tier ? a.tier < b.tier : a.glosses.size() > b.glosses.size();
        });

    const Entry& first = entries.first();
    QStringList meanings;
    for (const Entry& e : entries)
        for (const QString& g : e.glosses) {
            const QString s = shortGloss(g, e.pos);
            if (!s.isEmpty() && !meanings.contains(s, Qt::CaseInsensitive)) meanings << s;
        }
    if (isMonolingual()) {
        // A dictionary of definitions (English -> English): the first definition, then the next ones numbered.
        r.translation = entries.first().glosses.value(0);
        QStringList more;
        for (const Entry& e : entries)
            for (const QString& g : e.glosses)
                if (g != r.translation && more.size() < 3 && !more.contains(g)) more << g;
        for (int i = 0; i < more.size(); ++i) r.definition += (i ? QStringLiteral("\n") : QString()) + QStringLiteral("%1. %2").arg(i + 2).arg(more[i]);
        if (asForm) {
            // The word in its own right, after the word it's a form of ("building": also a structure).
            for (const Entry& e : own)
                if (e.tier <= 1 && !e.glosses.isEmpty()) {
                    const QString line = tr("Also on its own (%1): %2").arg(posName(e.pos), e.glosses.first());
                    r.definition = r.definition.isEmpty() ? line : line + QLatin1Char('\n') + r.definition;
                    break;
                }
        }
        if (!asForm && !also.lemma.isEmpty()) {
            QList<Entry> base = entriesFor(db, also.lemma, 1);
            std::stable_partition(base.begin(), base.end(), [&](const Entry& e) { return e.pos == also.pos; });
            if (!base.isEmpty()) {
                const QString line = tr("Also the %1 of “%2”: %3").arg(also.description, also.lemma, base.first().glosses.value(0));
                r.definition = r.definition.isEmpty() ? line : line + QLatin1Char('\n') + r.definition;
            }
        }
    } else {
        r.translation = meanings.value(0);
        for (int i = 1; i < meanings.size() && r.alternatives.size() < 4; ++i)
            if (meanings[i].size() <= 40) r.alternatives << meanings[i];
    }
    QStringList pos{posName(first.pos)};
    for (const QString& tag : {QStringLiteral("masculine"), QStringLiteral("feminine")})
        if (first.tags.split(QLatin1Char(' ')).contains(tag)) pos << tag;
    if (asForm) {
        // "first-person singular preterite indicative of piar" -> "first-person singular preterite indicative"
        QString d = formDescription;
        const int of = int(d.lastIndexOf(QStringLiteral(" of ")));
        if (of > 0) d = d.left(of);
        pos = QStringList{posName(formPos.isEmpty() ? first.pos : formPos)};
        if (!d.trimmed().isEmpty()) pos << d.trimmed();
    }
    r.partOfSpeech = pos.join(QStringLiteral(", "));
    r.lemma = lemma.compare(r.text, Qt::CaseInsensitive) == 0 ? QString() : lemma;
    r.provider = tr("Offline dictionary");
    return r;
}

namespace {

// A word as selected, without the punctuation around it ("¿Qué?", "«hidalgo»,"), and an English possessive's "'s"
// when the dictionary doesn't have the whole word ("Shakespeare's", but "o'clock").
QString bareWord(const QSqlDatabase& db, const QString& language, const QString& text)
{
    QString word = text.trimmed();
    word.remove(QRegularExpression(QStringLiteral("^[\\p{P}\\s]+|[\\p{P}\\s]+$")));
    if (language == QLatin1String("en") && word.size() > 3 &&
        (word.endsWith(QLatin1String("'s"), Qt::CaseInsensitive) || word.endsWith(QStringLiteral("’s"), Qt::CaseInsensitive)) &&
        entriesFor(db, word, 1).isEmpty())
        word.chop(2);
    return word;
}

DictionaryEntry cardEntry(const Entry& e)
{
    DictionaryEntry d;
    if (!e.card.isEmpty()) {
        const QJsonObject o = QJsonDocument::fromJson(e.card.toUtf8()).object();
        d.partOfSpeech = o.value(QStringLiteral("pos")).toString();
        d.origin = o.value(QStringLiteral("etymology")).toString();
        d.forms = o.value(QStringLiteral("forms")).toString();
        d.pronunciation = o.value(QStringLiteral("ipa")).toString();
        for (const QJsonValue& v : o.value(QStringLiteral("senses")).toArray()) {
            const QJsonObject s = v.toObject();
            DictionarySense sense;
            sense.gloss = s.value(QStringLiteral("gloss")).toString();
            for (const QJsonValue& t : s.value(QStringLiteral("tags")).toArray()) sense.labels << t.toString();
            for (const QJsonValue& x : s.value(QStringLiteral("examples")).toArray()) sense.examples << x.toString();
            if (!sense.gloss.isEmpty()) d.senses << sense;
        }
    }
    if (d.senses.isEmpty()) {
        // A dictionary without whole entries: its part of speech and glosses.
        QStringList pos{posName(e.pos)};
        for (const QString& tag : {QStringLiteral("masculine"), QStringLiteral("feminine")})
            if (e.tags.split(QLatin1Char(' ')).contains(tag)) pos << tag;
        d.partOfSpeech = pos.join(QStringLiteral(", "));
        for (const QString& g : e.glosses) d.senses << DictionarySense{g, {}, {}};
    }
    return d;
}

}  // namespace

DictionaryCard OfflineDictionary::card(const QString& text) const
{
    DictionaryCard c;
    c.text = text.trimmed();
    if (!m_info.ok || c.text.isEmpty()) return c;
    const QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    const QString word = bareWord(db, m_info.language, c.text);
    if (word.isEmpty() || word.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() > 4) return c;
    c.text = word;
    const bool english = m_info.language == QLatin1String("en");

    const QList<Entry> own = entriesFor(db, word, 2, m_info.cards);
    int ownTier = 99;
    for (const Entry& e : own) ownTier = qMin(ownTier, e.tier);
    // The words it is a form of, each once, at its closest spelling ("casas": casa and casar).
    struct Form {
        QString lemma, pos, description;
        int tier;
    };
    QList<Form> forms;
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT form, lemma, pos, description FROM forms WHERE folded=?"));
        q.addBindValue(practice::foldAccents(word.toLower()));
        if (q.exec())
            while (q.next()) {
                const int t = tierOf(q.value(0).toString(), word);
                const QString lemma = q.value(1).toString();
                auto it = std::find_if(forms.begin(), forms.end(), [&](const Form& f) { return f.lemma == lemma; });
                if (it == forms.end()) {
                    forms << Form{lemma, q.value(2).toString(), q.value(3).toString(), t};
                } else {
                    if (t < it->tier) *it = Form{lemma, q.value(2).toString(), q.value(3).toString(), t};
                    else if (it->description.isEmpty()) it->description = q.value(3).toString();
                }
            }
    }
    if (english) {
        // WordNet lists only irregular forms; regular ones by their endings ("walked" -> "walk").
        if (forms.isEmpty()) {
            const RegularForm f = regularEnglishForm(db, word.toLower());
            if (!f.lemma.isEmpty()) forms << Form{f.lemma, f.pos, f.description, 1};
        }
        for (Form& f : forms)
            if (f.description.isEmpty()) f.description = englishFormName(word.toLower(), f.pos);
    }
    int formTier = 99;
    for (const Form& f : forms) formTier = qMin(formTier, f.tier);

    auto headword = [&](QList<Entry> entries, const Form* form) {
        DictionaryHeadword h;
        if (entries.isEmpty()) return h;
        h.word = entries.first().word;
        if (form) {
            h.formOf = word;
            // "first-person singular preterite indicative of piar": the form without "of piar".
            QString d = form->description.trimmed();
            const int of = int(d.lastIndexOf(QStringLiteral(" of ")));
            if (english && of > 0) d = d.left(of);
            h.formNote = d;
            if (!form->pos.isEmpty())
                std::stable_partition(entries.begin(), entries.end(), [&](const Entry& e) { return e.pos == form->pos; });
        }
        // WordNet lists parts of speech in no useful order: the one with the most meanings first. Wiktionary's own
        // order is kept.
        if (isMonolingual() && !m_info.cards)
            std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.glosses.size() > b.glosses.size(); });
        for (const Entry& e : entries) h.entries << cardEntry(e);
        return h;
    };
    auto senseCount = [](const DictionaryHeadword& h) {
        qsizetype n = 0;
        for (const DictionaryEntry& e : h.entries) n += e.senses.size();
        return n;
    };

    // The word in its own right (its closest spellings, each spelling a headword: "Mancha", then "mancha"), unless
    // it's a form spelled more closely ("tenía" is a form of tener, not "tenia").
    QList<DictionaryHeadword> mine, theirs;
    if (ownTier <= formTier) {
        QList<Entry> group;
        for (const Entry& e : own) {
            if (!(e.tier == ownTier || (ownTier == 0 && e.tier == 1))) continue;
            if (!group.isEmpty() && group.first().word != e.word) {
                mine << headword(group, nullptr);
                group.clear();
            }
            group << e;
        }
        if (!group.isEmpty()) mine << headword(group, nullptr);
    }
    if (formTier <= ownTier) {
        for (const Form& f : forms) {
            if (f.tier != formTier || f.lemma.compare(word, Qt::CaseInsensitive) == 0) continue;
            if (std::any_of(mine.begin(), mine.end(), [&](const DictionaryHeadword& h) { return h.word.compare(f.lemma, Qt::CaseInsensitive) == 0; }))
                continue;
            QList<Entry> entries = entriesFor(db, f.lemma, 1, m_info.cards);
            if (entries.isEmpty()) entries = entriesFor(db, f.lemma, 2, m_info.cards);
            const DictionaryHeadword h = headword(entries, &f);
            if (!h.entries.isEmpty()) theirs << h;
        }
    }
    // English: a word that is also a form of a commoner one ("flies") shows the commoner one first.
    const bool formsFirst = !theirs.isEmpty() && (mine.isEmpty() || (english && senseCount(theirs.first()) >= senseCount(mine.first())));
    c.headwords = formsFirst ? theirs + mine : mine + theirs;
    while (c.headwords.size() > 4) c.headwords.removeLast();
    return c;
}

bool OfflineDictionary::handles(const TranslationRequest& request) const
{
    // Words only: an explanation or a page of paragraphs is for the AI providers.
    if (request.kind != TranslationRequest::Kind::Lookup) return false;
    return request.from.isEmpty() || m_info.language.isEmpty() || request.from == m_info.language;
}

void OfflineDictionary::translate(const TranslationRequest& request, Done done)
{
    // A dictionary has words, not passages.
    const TranslationResult r = isSetUp() && request.text.simplified().split(QLatin1Char(' ')).size() <= 4 ? lookUp(request.text, request.from) : TranslationResult();
    const TranslationFailure f = !isSetUp() ? TranslationFailure::NotSetUp : r.translation.isEmpty() ? TranslationFailure::NotFound : TranslationFailure::None;
    QTimer::singleShot(0, this, [done, r, f] { done(r, f, {}); });
}

}  // namespace core

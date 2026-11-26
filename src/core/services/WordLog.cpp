#include "WordLog.h"
#include "core/practice/Answer.h"
#include <random>
#include <QRandomGenerator>

#include "Library.h"
#include "core/translate/Translation.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QVariant>

#include <algorithm>

namespace core {

namespace {

QString iso(const QDateTime& t) { return t.isValid() ? t.toUTC().toString(Qt::ISODateWithMs) : QString(); }
QDateTime fromIso(const QString& s)
{
    if (s.isEmpty()) return {};
    QDateTime t = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!t.isValid()) t = QDateTime::fromString(s, Qt::ISODate);
    return t.toUTC();
}

// SQLite gets an empty string, never NULL, for an empty QString (the text columns are NOT NULL).
QVariant bindable(const QString& s) { return s.isNull() ? QVariant(QStringLiteral("")) : QVariant(s); }
QVariant bindable(const QVariant& v) { return v; }
QVariant bindable(int v) { return v; }
QVariant bindable(double v) { return v; }

int wordCount(const QString& s) { return int(s.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts).size()); }

bool isPassage(const QString& text) { return wordCount(text) > 6 || text.simplified().size() > 60; }

QString keyFor(const QString& text, const QString& lemma, bool passage)
{
    if (passage) return QStringLiteral("p:") + text.simplified().toLower().left(400);
    return (lemma.isEmpty() ? text : lemma).simplified().toLower();
}

const char* kWordColumns =
    "SELECT w.id, w.language, w.text, w.lemma, w.passage, w.translation, w.alternatives, w.part_of_speech, w.definition, "
    "w.sentence, w.sentence_translation, w.note, w.user_note, w.edited, w.starred, COALESCE(w.book_id, 0), w.book_title, "
    "w.spine, w.sel_start, w.sel_end, w.cfi, w.page_label, w.chapter, w.lookups, w.added, w.last_seen, w.meaning_language FROM words w ";

WordEntry readWord(const QSqlQuery& q)
{
    WordEntry e;
    e.id = q.value(0).toInt();
    e.language = q.value(1).toString();
    e.text = q.value(2).toString();
    e.lemma = q.value(3).toString();
    e.passage = q.value(4).toInt() != 0;
    e.translation = q.value(5).toString();
    for (const QJsonValue& v : QJsonDocument::fromJson(q.value(6).toString().toUtf8()).array()) e.alternatives << v.toString();
    e.partOfSpeech = q.value(7).toString();
    e.definition = q.value(8).toString();
    e.sentence = q.value(9).toString();
    e.sentenceTranslation = q.value(10).toString();
    e.note = q.value(11).toString();
    e.userNote = q.value(12).toString();
    e.edited = q.value(13).toInt() != 0;
    e.starred = q.value(14).toInt() != 0;
    e.bookId = q.value(15).toInt();
    e.bookTitle = q.value(16).toString();
    e.spine = q.value(17).toInt();
    e.start = q.value(18).toInt();
    e.end = q.value(19).toInt();
    e.cfi = q.value(20).toString();
    e.pageLabel = q.value(21).toString();
    e.chapter = q.value(22).toString();
    e.lookups = q.value(23).toInt();
    e.added = fromIso(q.value(24).toString());
    e.lastSeen = fromIso(q.value(25).toString());
    e.meaningLanguage = q.value(26).toString();
    return e;
}

const char* kCardColumns =
    "SELECT id, word_id, direction, state, step, due, stability, difficulty, reps, lapses, last_review, suspended FROM cards ";

PracticeCard readCard(const QSqlQuery& q)
{
    PracticeCard c;
    c.id = q.value(0).toInt();
    c.wordId = q.value(1).toInt();
    c.direction = q.value(2).toString();
    c.memory.state = practice::CardState(q.value(3).toInt());
    c.memory.step = q.value(4).toInt();
    c.memory.due = fromIso(q.value(5).toString());
    c.memory.stability = q.value(6).toDouble();
    c.memory.difficulty = q.value(7).toDouble();
    c.memory.reps = q.value(8).toInt();
    c.memory.lapses = q.value(9).toInt();
    c.memory.lastReview = fromIso(q.value(10).toString());
    c.suspended = q.value(11).toInt() != 0;
    return c;
}

// Whether a word can be practised in a direction (a definition card needs a definition, and so on).
bool fits(const WordEntry& w, const QString& direction)
{
    // A highlighted word with no meaning yet: shown in the book's language, the meaning found on the card.
    if (w.translation.isEmpty()) return !w.passage && direction == QLatin1String("recognize");
    if (w.passage) return direction == QLatin1String("recognize");
    if (direction == QLatin1String("definition")) return !w.definition.isEmpty();
    if (direction == QLatin1String("cloze")) return !w.sentence.isEmpty() && w.sentence.contains(w.text, Qt::CaseInsensitive) && w.sentence.simplified().size() > w.text.size() + 3;
    return true;
}

QDateTime startOfLocalDay(const QDateTime& nowUtc) { return QDateTime(nowUtc.toLocalTime().date(), QTime(0, 0)).toUTC(); }

}  // namespace

WordLog::WordLog(Library* library) : m_connection(library ? library->connectionName() : QString()) {}

bool WordLog::isOpen() const { return !m_connection.isEmpty() && QSqlDatabase::database(m_connection, false).isOpen(); }

int WordLog::record(const TranslationResult& r, const WordPlace& place, const QDateTime& now)
{
    if (r.translation.trimmed().isEmpty()) return 0;
    return save(r, place, now, true);
}

int WordLog::mark(const TranslationResult& meaning, const WordPlace& place, const QDateTime& now)
{
    return save(meaning, place, now, false);
}

int WordLog::save(const TranslationResult& r, const WordPlace& place, const QDateTime& now, bool lookup)
{
    if (!isOpen() || r.text.trimmed().isEmpty()) return 0;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    const QString text = r.text.simplified();
    const bool passage = isPassage(text);
    const QString lemma = passage ? QString() : r.lemma.simplified();
    const QString language = place.language.isEmpty() ? QStringLiteral("und") : place.language;
    const QString key = keyFor(text, lemma, passage);
    const QString alts = QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(r.alternatives)).toJson(QJsonDocument::Compact));

    // A highlighted word that was never looked up is filed under the word as read (or the offline dictionary's
    // dictionary form): the first lookup that brings another dictionary form takes that entry over instead of
    // starting a second one.
    if (lookup && !passage && !lemma.isEmpty() && key != keyFor(text, QString(), false)) {
        QSqlQuery p(QSqlDatabase::database(m_connection));
        p.prepare(QStringLiteral("UPDATE words SET word_key=?, lemma=? WHERE language=? AND word_key=? AND lookups=0 AND NOT EXISTS "
                                 "(SELECT 1 FROM words w2 WHERE w2.language=? AND w2.word_key=?)"));
        p.addBindValue(bindable(key));
        p.addBindValue(bindable(lemma));
        p.addBindValue(bindable(language));
        p.addBindValue(bindable(keyFor(text, QString(), false)));
        p.addBindValue(bindable(language));
        p.addBindValue(bindable(key));
        p.exec();
    }

    int found = 0;
    bool wasEdited = false, hasMeaning = false;
    q.prepare(QStringLiteral("SELECT id, edited, translation FROM words WHERE language=? AND word_key=?"));
    q.addBindValue(bindable(language));
    q.addBindValue(bindable(key));
    bool hit = q.exec() && q.next();
    if (!hit && !lookup && !passage) {
        // Highlighted again: the entry of the same word as read, whatever dictionary form a lookup gave it.
        q.prepare(QStringLiteral("SELECT id, edited, translation FROM words WHERE language=? AND text=? AND passage=0 ORDER BY last_seen DESC LIMIT 1"));
        q.addBindValue(bindable(language));
        q.addBindValue(bindable(text));
        hit = q.exec() && q.next();
    }
    if (hit) {
        found = q.value(0).toInt();
        wasEdited = q.value(1).toInt() != 0;
        hasMeaning = !q.value(2).toString().isEmpty();
    }
    if (found) {
        const int id = found;
        // A hand-edited meaning stays; a highlight only fills a meaning that is missing.
        const bool edited = wasEdited || r.translation.trimmed().isEmpty() || (!lookup && hasMeaning);
        QSqlQuery u(QSqlDatabase::database(m_connection));
        // The sentence's English stays when the sentence is the same and this answer has none (a highlight).
        u.prepare(QStringLiteral("UPDATE words SET text=?, lookups=lookups+?, last_seen=?, sentence_translation=CASE WHEN ?='' AND sentence=? "
                                 "THEN sentence_translation ELSE ? END, sentence=?, book_id=?, book_title=?,"
                                 " spine=?, sel_start=?, sel_end=?, cfi=?, page_label=?, chapter=? WHERE id=?"));
        u.addBindValue(bindable(text));
        u.addBindValue(bindable(lookup ? 1 : 0));
        u.addBindValue(bindable(iso(now)));
        u.addBindValue(bindable(r.inSentence));
        u.addBindValue(bindable(place.sentence.simplified()));
        u.addBindValue(bindable(r.inSentence));
        u.addBindValue(bindable(place.sentence.simplified()));
        u.addBindValue(bindable(place.bookId > 0 ? QVariant(place.bookId) : QVariant(QMetaType(QMetaType::Int))));
        u.addBindValue(bindable(place.bookTitle));
        u.addBindValue(bindable(place.spine));
        u.addBindValue(bindable(place.start));
        u.addBindValue(bindable(place.end));
        u.addBindValue(bindable(place.cfi));
        u.addBindValue(bindable(place.pageLabel));
        u.addBindValue(bindable(place.chapter));
        u.addBindValue(bindable(id));
        u.exec();
        if (!edited) {
            // The latest machine answer; a meaning changed by hand stays.
            QSqlQuery m(QSqlDatabase::database(m_connection));
            m.prepare(QStringLiteral("UPDATE words SET translation=?, alternatives=?, part_of_speech=?, definition=?, note=?, meaning_language=? WHERE id=?"));
            m.addBindValue(bindable(r.translation.trimmed()));
            m.addBindValue(bindable(alts));
            m.addBindValue(bindable(r.partOfSpeech));
            m.addBindValue(bindable(r.definition));
            m.addBindValue(bindable(r.note));
            m.addBindValue(bindable(languages::readerLanguage()));
            m.addBindValue(bindable(id));
            m.exec();
        }
        return id;
    }
    q.prepare(QStringLiteral("INSERT INTO words (language, word_key, text, lemma, passage, translation, alternatives, part_of_speech, definition,"
                             " sentence, sentence_translation, note, book_id, book_title, spine, sel_start, sel_end, cfi, page_label, chapter,"
                             " lookups, added, last_seen, meaning_language) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(bindable(language));
    q.addBindValue(bindable(key));
    q.addBindValue(bindable(text));
    q.addBindValue(bindable(lemma));
    q.addBindValue(bindable(passage ? 1 : 0));
    q.addBindValue(bindable(r.translation.trimmed()));
    q.addBindValue(bindable(alts));
    q.addBindValue(bindable(r.partOfSpeech));
    q.addBindValue(bindable(r.definition));
    q.addBindValue(bindable(place.sentence.simplified()));
    q.addBindValue(bindable(r.inSentence));
    q.addBindValue(bindable(r.note));
    q.addBindValue(bindable(place.bookId > 0 ? QVariant(place.bookId) : QVariant(QMetaType(QMetaType::Int))));
    q.addBindValue(bindable(place.bookTitle));
    q.addBindValue(bindable(place.spine));
    q.addBindValue(bindable(place.start));
    q.addBindValue(bindable(place.end));
    q.addBindValue(bindable(place.cfi));
    q.addBindValue(bindable(place.pageLabel));
    q.addBindValue(bindable(place.chapter));
    q.addBindValue(bindable(lookup ? 1 : 0));
    q.addBindValue(bindable(iso(now)));
    q.addBindValue(bindable(iso(now)));
    q.addBindValue(bindable(languages::readerLanguage()));
    if (!q.exec()) {
        qWarning("word log: %s", qPrintable(q.lastError().text()));
        return 0;
    }
    return q.lastInsertId().toInt();
}

bool WordLog::fillMeaning(int wordId, const TranslationResult& r)
{
    if (!isOpen() || wordId <= 0 || r.translation.trimmed().isEmpty()) return false;
    const WordEntry w = word(wordId);
    if (w.id == 0 || w.edited) return false;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("UPDATE words SET translation=?, alternatives=?, part_of_speech=?, definition=?, note=?, lookups=lookups+1, "
                             "sentence_translation=CASE WHEN ?='' THEN sentence_translation ELSE ? END, meaning_language=? WHERE id=?"));
    q.addBindValue(bindable(r.translation.trimmed()));
    q.addBindValue(bindable(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(r.alternatives)).toJson(QJsonDocument::Compact))));
    q.addBindValue(bindable(r.partOfSpeech));
    q.addBindValue(bindable(r.definition));
    q.addBindValue(bindable(r.note));
    q.addBindValue(bindable(r.inSentence));
    q.addBindValue(bindable(r.inSentence));
    q.addBindValue(bindable(languages::readerLanguage()));
    q.addBindValue(bindable(wordId));
    if (!q.exec()) return false;
    // The dictionary form, when the entry had none and no other entry is filed under it.
    const QString lemma = r.lemma.simplified();
    if (!w.passage && w.lemma.isEmpty() && !lemma.isEmpty() && lemma.compare(w.text, Qt::CaseInsensitive) != 0) {
        QSqlQuery l(QSqlDatabase::database(m_connection));
        l.prepare(QStringLiteral("UPDATE words SET lemma=?, word_key=? WHERE id=? AND NOT EXISTS (SELECT 1 FROM words w2 WHERE w2.language=? AND w2.word_key=?)"));
        l.addBindValue(bindable(lemma));
        l.addBindValue(bindable(keyFor(w.text, lemma, false)));
        l.addBindValue(bindable(wordId));
        l.addBindValue(bindable(w.language));
        l.addBindValue(bindable(keyFor(w.text, lemma, false)));
        l.exec();
    }
    return true;
}

QList<WordEntry> WordLog::words(int bookId) const
{
    QList<WordEntry> out;
    if (!isOpen()) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (bookId > 0) {
        q.prepare(QString::fromLatin1(kWordColumns) + QStringLiteral("WHERE w.book_id=? ORDER BY w.last_seen DESC, w.id DESC"));
        q.addBindValue(bindable(bookId));
    } else {
        q.prepare(QString::fromLatin1(kWordColumns) + QStringLiteral("ORDER BY w.last_seen DESC, w.id DESC"));
    }
    if (q.exec())
        while (q.next()) out << readWord(q);
    return out;
}

WordEntry WordLog::word(int id) const
{
    if (!isOpen()) return {};
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QString::fromLatin1(kWordColumns) + QStringLiteral("WHERE w.id=?"));
    q.addBindValue(bindable(id));
    return q.exec() && q.next() ? readWord(q) : WordEntry();
}

WordEntry WordLog::find(const QString& language, const QString& text, const QString& lemma, const QString& meaningLanguage) const
{
    if (!isOpen() || text.trimmed().isEmpty()) return {};
    const QString lang = language.isEmpty() ? QStringLiteral("und") : language;
    const QString meaningLang = meaningLanguage.isEmpty() ? languages::readerLanguage() : meaningLanguage;
    // Its key with or without the dictionary form, or the same word saved under another dictionary form (one
    // answer said "tener", another none). Several saved meanings: the one seen last.
    const QString key = keyFor(text.simplified(), QString(), false);
    const QString lemmaKey = lemma.trimmed().isEmpty() ? key : keyFor(text.simplified(), lemma.simplified(), false);
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QString::fromLatin1(kWordColumns) +
              QStringLiteral("WHERE w.language=? AND w.translation<>'' AND w.meaning_language=? "
                             "AND (w.word_key=? OR w.word_key=? OR (w.passage=0 AND w.text=? COLLATE NOCASE)) "
                             "ORDER BY w.last_seen DESC, w.id DESC LIMIT 1"));
    q.addBindValue(bindable(lang));
    q.addBindValue(bindable(meaningLang));
    q.addBindValue(bindable(key));
    q.addBindValue(bindable(lemmaKey));
    q.addBindValue(bindable(text.simplified()));
    return q.exec() && q.next() ? readWord(q) : WordEntry();
}

bool WordLog::update(const WordEntry& e)
{
    if (!isOpen() || e.id <= 0) return false;
    const WordEntry old = word(e.id);
    if (old.id == 0) return false;
    const bool meaningChanged = old.translation != e.translation.trimmed() || old.definition != e.definition.trimmed() ||
                                old.alternatives != e.alternatives;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    // A meaning typed by hand is in the reader's language now; a star or a note leaves the meaning's language.
    q.prepare(QStringLiteral("UPDATE words SET translation=?, alternatives=?, definition=?, user_note=?, starred=?, edited=?, meaning_language=? WHERE id=?"));
    q.addBindValue(bindable(e.translation.trimmed()));
    q.addBindValue(bindable(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(e.alternatives)).toJson(QJsonDocument::Compact))));
    q.addBindValue(bindable(e.definition.trimmed()));
    q.addBindValue(bindable(e.userNote));
    q.addBindValue(bindable(e.starred ? 1 : 0));
    q.addBindValue(bindable(old.edited || meaningChanged ? 1 : 0));
    q.addBindValue(bindable(meaningChanged ? languages::readerLanguage() : old.meaningLanguage));
    q.addBindValue(bindable(e.id));
    return q.exec();
}

void WordLog::remove(int id)
{
    if (!isOpen()) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("DELETE FROM words WHERE id=?"));
    q.addBindValue(bindable(id));
    q.exec();
}

int WordLog::count() const
{
    if (!isOpen()) return 0;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    return q.exec(QStringLiteral("SELECT COUNT(*) FROM words")) && q.next() ? q.value(0).toInt() : 0;
}

QList<QPair<int, QString>> WordLog::booksWithWords() const
{
    QList<QPair<int, QString>> out;
    if (!isOpen()) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (q.exec(QStringLiteral("SELECT book_id, MAX(book_title) FROM words WHERE book_id IS NOT NULL GROUP BY book_id ORDER BY MAX(book_title) COLLATE NOCASE")))
        while (q.next()) out << qMakePair(q.value(0).toInt(), q.value(1).toString());
    return out;
}

QHash<int, QDateTime> WordLog::nextReviews() const
{
    QHash<int, QDateTime> out;
    if (!isOpen()) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (q.exec(QStringLiteral("SELECT word_id, MIN(due) FROM cards WHERE suspended=0 AND state<>0 GROUP BY word_id")))
        while (q.next()) out.insert(q.value(0).toInt(), fromIso(q.value(1).toString()));
    return out;
}

QList<PracticeCard> WordLog::cardsFor(int wordId) const
{
    QList<PracticeCard> out;
    if (!isOpen()) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QString::fromLatin1(kCardColumns) + QStringLiteral("WHERE word_id=? ORDER BY id"));
    q.addBindValue(bindable(wordId));
    if (q.exec())
        while (q.next()) out << readCard(q);
    return out;
}

PracticeCard WordLog::card(int id) const
{
    if (!isOpen()) return {};
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QString::fromLatin1(kCardColumns) + QStringLiteral("WHERE id=?"));
    q.addBindValue(bindable(id));
    return q.exec() && q.next() ? readCard(q) : PracticeCard();
}

PracticeCard WordLog::ensureCard(int wordId, const QString& direction, const QDateTime& now)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO cards (word_id, direction, due) VALUES (?,?,?)"));
    q.addBindValue(bindable(wordId));
    q.addBindValue(bindable(direction));
    q.addBindValue(bindable(iso(now)));
    q.exec();
    q.prepare(QString::fromLatin1(kCardColumns) + QStringLiteral("WHERE word_id=? AND direction=?"));
    q.addBindValue(bindable(wordId));
    q.addBindValue(bindable(direction));
    return q.exec() && q.next() ? readCard(q) : PracticeCard();
}

namespace {

struct Pool {
    QList<WordEntry> words;
    QHash<QPair<int, QString>, PracticeCard> cards;   // (word id, direction)
};

}  // namespace

static Pool loadPool(const QString& connection, const WordLog::Filter& f)
{
    Pool p;
    QSqlQuery q(QSqlDatabase::database(connection));
    q.prepare(QString::fromLatin1(kWordColumns) + QStringLiteral("ORDER BY w.added, w.id"));
    if (q.exec())
        while (q.next()) {
            const WordEntry w = readWord(q);
            if (f.bookId > 0 && w.bookId != f.bookId) continue;
            if (w.passage && !f.passages) continue;
            if (f.starredOnly && !w.starred) continue;
            p.words << w;
        }
    QSqlQuery c(QSqlDatabase::database(connection));
    if (c.exec(QString::fromLatin1(kCardColumns)))
        while (c.next()) {
            const PracticeCard card = readCard(c);
            p.cards.insert(qMakePair(card.wordId, card.direction), card);
        }
    return p;
}

PracticeCounts WordLog::counts(const Filter& f, const QDateTime& now) const
{
    PracticeCounts out;
    if (!isOpen()) return out;
    const Pool p = loadPool(m_connection, f);
    const QDateTime dayStart = startOfLocalDay(now);
    QSet<int> startedToday;
    {
        QSqlQuery q(QSqlDatabase::database(m_connection));
        q.prepare(QStringLiteral("SELECT DISTINCT card_id FROM reviews WHERE state=0 AND reviewed>=?"));
        q.addBindValue(bindable(iso(dayStart)));
        if (q.exec())
            while (q.next()) startedToday.insert(q.value(0).toInt());
    }
    for (const WordEntry& w : p.words) {
        bool any = false;
        for (const QString& d : f.directions) {
            if (!fits(w, d)) continue;
            any = true;
            const auto it = p.cards.constFind(qMakePair(w.id, d));
            if (it == p.cards.constEnd() || it->memory.state == practice::CardState::New) {
                if (it == p.cards.constEnd() || !it->suspended) ++out.newAvailable;
                continue;
            }
            if (it->suspended) continue;
            if (startedToday.contains(it->id)) ++out.newToday;
            if (it->memory.due <= now) ++out.dueNow;
            if (it->memory.state == practice::CardState::Review) ++out.learned;
        }
        if (any) ++out.words;
    }
    return out;
}

QList<PracticeCard> WordLog::session(const Filter& f, int newLimit, const QDateTime& now, int maxCards)
{
    QList<PracticeCard> out;
    if (!isOpen()) return out;
    const PracticeCounts c = counts(f, now);
    const Pool p = loadPool(m_connection, f);
    QList<PracticeCard> learning, reviews;
    QList<QPair<int, QString>> fresh;   // new (word, direction), oldest word first
    for (const WordEntry& w : p.words)
        for (const QString& d : f.directions) {
            if (!fits(w, d)) continue;
            const auto it = p.cards.constFind(qMakePair(w.id, d));
            if (it != p.cards.constEnd() && it->suspended) continue;
            if (it == p.cards.constEnd() || it->memory.state == practice::CardState::New) {
                fresh << qMakePair(w.id, d);
                continue;
            }
            if (it->memory.due > now) continue;
            (it->memory.state == practice::CardState::Review ? reviews : learning) << *it;
        }
    auto byDue = [](const PracticeCard& a, const PracticeCard& b) { return a.memory.due < b.memory.due; };
    std::sort(learning.begin(), learning.end(), byDue);
    std::sort(reviews.begin(), reviews.end(), byDue);
    out << learning;
    const int allowedNew = std::max(0, newLimit - c.newToday);
    QList<PracticeCard> news;
    for (int i = 0; i < fresh.size() && news.size() < allowedNew; ++i) {
        const PracticeCard card = ensureCard(fresh[i].first, fresh[i].second, now);
        if (card.id) news << card;
    }
    // Reviews and new cards mixed: a new card after every three reviews.
    int ri = 0, ni = 0;
    while (ri < reviews.size() || ni < news.size()) {
        for (int k = 0; k < 3 && ri < reviews.size(); ++k) out << reviews[ri++];
        if (ni < news.size()) out << news[ni++];
    }
    while (out.size() > maxCards) out.removeLast();
    return out;
}

QList<PracticeCard> WordLog::allCards(const Filter& f, const QString& order, const QDateTime& now)
{
    QList<PracticeCard> out;
    if (!isOpen()) return out;
    const Pool p = loadPool(m_connection, f);
    QList<WordEntry> words = p.words;   // oldest first
    if (order == QLatin1String("newest")) {
        std::reverse(words.begin(), words.end());
    } else if (order == QLatin1String("alphabetical")) {
        std::stable_sort(words.begin(), words.end(), [](const WordEntry& a, const WordEntry& b) {
            return QString::localeAwareCompare(practice::foldAccents(a.headword().toLower()), practice::foldAccents(b.headword().toLower())) < 0;
        });
    } else if (order == QLatin1String("book")) {
        std::stable_sort(words.begin(), words.end(), [](const WordEntry& a, const WordEntry& b) {
            if (a.bookTitle != b.bookTitle) return QString::localeAwareCompare(a.bookTitle, b.bookTitle) < 0;
            if (a.spine != b.spine) return a.spine < b.spine;
            return a.start < b.start;
        });
    } else if (order != QLatin1String("oldest")) {
        std::shuffle(words.begin(), words.end(), *QRandomGenerator::global());
    }
    for (const WordEntry& w : words)
        for (const QString& d : f.directions) {
            if (!fits(w, d)) continue;
            const auto it = p.cards.constFind(qMakePair(w.id, d));
            if (it != p.cards.constEnd() && it->suspended) continue;
            const PracticeCard card = it != p.cards.constEnd() ? *it : ensureCard(w.id, d, now);
            if (card.id) out << card;
        }
    return out;
}

int WordLog::practisableCount(const Filter& f) const
{
    if (!isOpen()) return 0;
    const Pool p = loadPool(m_connection, f);
    int n = 0;
    for (const WordEntry& w : p.words)
        for (const QString& d : f.directions) {
            if (!fits(w, d)) continue;
            const auto it = p.cards.constFind(qMakePair(w.id, d));
            if (it == p.cards.constEnd() || !it->suspended) ++n;
        }
    return n;
}

void WordLog::saveReview(const PracticeCard& before, const PracticeCard& after, practice::Rating rating, const QDateTime& now)
{
    if (!isOpen() || after.id <= 0) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("UPDATE cards SET state=?, step=?, due=?, stability=?, difficulty=?, reps=?, lapses=?, last_review=? WHERE id=?"));
    q.addBindValue(bindable(int(after.memory.state)));
    q.addBindValue(bindable(after.memory.step));
    q.addBindValue(bindable(iso(after.memory.due)));
    q.addBindValue(bindable(after.memory.stability));
    q.addBindValue(bindable(after.memory.difficulty));
    q.addBindValue(bindable(after.memory.reps));
    q.addBindValue(bindable(after.memory.lapses));
    q.addBindValue(bindable(iso(after.memory.lastReview)));
    q.addBindValue(bindable(after.id));
    q.exec();
    QSqlQuery r(QSqlDatabase::database(m_connection));
    r.prepare(QStringLiteral("INSERT INTO reviews (card_id, rating, state, reviewed, elapsed_days) VALUES (?,?,?,?,?)"));
    r.addBindValue(bindable(after.id));
    r.addBindValue(bindable(int(rating)));
    r.addBindValue(bindable(int(before.memory.state)));
    r.addBindValue(bindable(iso(now)));
    r.addBindValue(bindable(before.memory.lastReview.isValid() ? double(before.memory.lastReview.secsTo(now)) / 86400.0 : 0.0));
    r.exec();
}

void WordLog::setSuspended(int cardId, bool on)
{
    if (!isOpen()) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("UPDATE cards SET suspended=? WHERE id=?"));
    q.addBindValue(bindable(on ? 1 : 0));
    q.addBindValue(bindable(cardId));
    q.exec();
}

void WordLog::resetCards(int wordId)
{
    if (!isOpen()) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("DELETE FROM cards WHERE word_id=?"));
    q.addBindValue(bindable(wordId));
    q.exec();
}

int WordLog::reviewCount() const
{
    if (!isOpen()) return 0;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    return q.exec(QStringLiteral("SELECT COUNT(*) FROM reviews")) && q.next() ? q.value(0).toInt() : 0;
}

bool WordLog::exportTsv(const QString& path, int bookId, QString* error) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    auto clean = [](QString s) {
        s.replace(QLatin1Char('\t'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' '));
        return s.trimmed();
    };
    // Anki reads these header lines: tab-separated, plain text, the column names.
    ts << "#separator:tab\n#html:false\n";
    ts << "#columns:Word\tAs Read\tMeaning\tOther Meanings\tPart of Speech\tDefinition\tSentence\tSentence Meaning\tBook\tPage\tAdded\n";
    for (const WordEntry& w : words(bookId)) {
        ts << clean(w.headword()) << '\t' << clean(w.text) << '\t' << clean(w.translation) << '\t' << clean(w.alternatives.join(QStringLiteral(", ")))
           << '\t' << clean(w.partOfSpeech) << '\t' << clean(w.definition) << '\t' << clean(w.sentence) << '\t' << clean(w.sentenceTranslation)
           << '\t' << clean(w.bookTitle) << '\t' << clean(w.pageLabel) << '\t' << w.added.toLocalTime().date().toString(Qt::ISODate) << '\n';
    }
    ts.flush();
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

}  // namespace core

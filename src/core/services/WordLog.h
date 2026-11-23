// The word log and practice cards, in the library database.
// - Every word or passage looked up is logged: one entry per word (by its dictionary form, so "tenía" and
//   "tengo" are one entry, "tener"), with its meaning, definition, the last sentence and place it was seen, and
//   how often it was looked up. A hand-edited meaning is never replaced by a later lookup.
// - Practice cards: one per word and direction (book language -> English, English -> book language, definition
//   -> word, sentence with a gap -> word), each with its spaced-repetition state, created when first practised.
#pragma once

#include "core/practice/Scheduler.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace core {

class Library;
struct TranslationResult;

struct WordEntry {
    int id = 0;
    QString language;          // "es"
    QString text;              // as it was selected ("tenía")
    QString lemma;             // dictionary form ("tener"); empty when it is the text itself
    bool passage = false;      // more than a few words
    QString translation, partOfSpeech, definition, sentence, sentenceTranslation, note, userNote;
    QStringList alternatives;
    bool edited = false;       // the meaning was changed by hand
    bool starred = false;
    int bookId = 0;
    QString bookTitle, cfi, pageLabel, chapter;
    int spine = -1, start = 0, end = 0;
    int lookups = 1;
    QDateTime added, lastSeen;

    QString headword() const { return lemma.isEmpty() ? text : lemma; }
};

// Where a lookup happened, for the log entry.
struct WordPlace {
    int bookId = 0;
    QString bookTitle, cfi, pageLabel, chapter, language;
    int spine = -1, start = 0, end = 0;
    QString sentence;
};

struct PracticeCard {
    int id = 0;
    int wordId = 0;
    QString direction;         // see WordLog::directions()
    practice::CardMemory memory;
    bool suspended = false;
};

struct PracticeCounts {
    int words = 0;             // practisable words (passages only when included)
    int dueNow = 0;            // cards to review now (learning and review)
    int newAvailable = 0;      // words with no card yet in a chosen direction, or new cards
    int newToday = 0;          // new cards already started today
    int learned = 0;           // cards in review state
};

class WordLog {
public:
    explicit WordLog(Library* library);
    bool isOpen() const;

    static QStringList directions() { return {"recognize", "recall", "definition", "cloze"}; }

    // Logs a lookup (adds the word or updates it). Returns the entry's id, 0 when nothing was logged.
    int record(const TranslationResult& r, const WordPlace& place, const QDateTime& now = QDateTime::currentDateTimeUtc());
    // Logs a highlighted word without a lookup: `meaning` is the offline dictionary's answer, or has only `text`
    // (then the word waits for a meaning: practised Spanish -> English only, with a Look Up button on the card).
    // Not counted as a lookup; never replaces a meaning the word already has.
    int mark(const TranslationResult& meaning, const WordPlace& place, const QDateTime& now = QDateTime::currentDateTimeUtc());
    // A lookup's answer for one entry (the Look Up button on a card): its meaning, unless edited by hand.
    bool fillMeaning(int wordId, const TranslationResult& r);
    QList<WordEntry> words(int bookId = 0) const;   // newest first; 0: all books
    WordEntry word(int id) const;
    // A saved word with a meaning, found by how it was read ("tenía") or by its dictionary form ("tener"), in any
    // capitals; id 0 when there is none. For answering offline from what was looked up before.
    WordEntry find(const QString& language, const QString& text, const QString& lemma = QString()) const;
    bool update(const WordEntry& e);                 // meaning, note, star; marks the meaning as edited
    void remove(int id);                             // with its cards and their history
    int count() const;
    QList<QPair<int, QString>> booksWithWords() const;   // (book id, title), by title

    // Practice. `bookId` 0: all books. Passages are left out unless `passages`.
    struct Filter {
        QStringList directions = {"recognize"};
        int bookId = 0;
        bool passages = false;
        bool starredOnly = false;
    };
    PracticeCounts counts(const Filter& f, const QDateTime& now) const;
    // The next cards for a session: learning cards that are due, reviews that are due, then new cards up to
    // `newLimit` for today. Cards are created here for words that have none yet in a chosen direction.
    QList<PracticeCard> session(const Filter& f, int newLimit, const QDateTime& now, int maxCards = 200);
    PracticeCard card(int id) const;
    QList<PracticeCard> cardsFor(int wordId) const;
    void saveReview(const PracticeCard& before, const PracticeCard& after, practice::Rating rating, const QDateTime& now);
    void setSuspended(int cardId, bool on);
    void resetCards(int wordId);                     // start over: the word's cards become new again
    int reviewCount() const;

    // Export: tab-separated, UTF-8, one row per word, for Anki's import or a spreadsheet.
    bool exportTsv(const QString& path, int bookId, QString* error) const;

private:
    int save(const TranslationResult& r, const WordPlace& place, const QDateTime& now, bool lookup);
    PracticeCard ensureCard(int wordId, const QString& direction, const QDateTime& now);
    QString m_connection;
};

}  // namespace core

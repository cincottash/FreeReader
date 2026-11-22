// Reading and learning statistics, from the library database:
// - the reading log (time read, pages turned and characters read per book and local day, written by the reader),
// - the word log (words added) and practice (reviews, cards learned),
// - the library (books finished and in progress).
#pragma once

#include <QDate>
#include <QList>
#include <QString>

namespace core {

class Library;

class ReadingLog {
public:
    explicit ReadingLog(Library* library);
    bool isOpen() const;
    // Adds to the book's totals for that day.
    void add(int bookId, const QDate& day, int seconds, int pages, qint64 chars);

private:
    QString m_connection;
};

struct DayStats {
    QDate day;
    int readingSeconds = 0;
    int pages = 0;
    int newWords = 0;     // words added to the word log (looked up or highlighted)
    int reviews = 0;      // practice cards answered
};

struct BookStats {
    int bookId = 0;
    QString title, author;
    int seconds = 0, pages = 0, words = 0;
    double progress = 0;   // 0..1
    bool finished = false;
};

struct Statistics {
    int todaySeconds = 0, weekSeconds = 0, monthSeconds = 0, totalSeconds = 0;   // week: the last 7 days, month: 30
    int pagesWeek = 0;
    int streak = 0, bestStreak = 0;       // days in a row with reading (a minute or more) or practice
    int booksFinished = 0, booksReading = 0;
    int words = 0, wordsWeek = 0;
    int cardsLearned = 0;                 // cards in review (past the learning steps)
    int reviewsToday = 0, reviewsMonth = 0;
    int retentionMonth = -1;              // % of the last 30 days' reviews answered Hard, Good or Easy; -1: none
    QList<DayStats> days;                 // the last `days` days, oldest first, today last
    QList<BookStats> books;               // books read or with words, most time first
};

// `today`: the local date the figures end on (tests pass their own).
Statistics computeStatistics(Library* library, const QDate& today, int days = 30);

}  // namespace core

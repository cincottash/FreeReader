#include "Statistics.h"

#include "Library.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

namespace core {

namespace {

// Words and reviews are stored with UTC times; statistics count them by the reader's local day.
QDate localDay(const QString& iso)
{
    QDateTime t = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!t.isValid()) t = QDateTime::fromString(iso, Qt::ISODate);
    return t.isValid() ? t.toLocalTime().date() : QDate();
}

}  // namespace

ReadingLog::ReadingLog(Library* library) : m_connection(library ? library->connectionName() : QString()) {}

bool ReadingLog::isOpen() const { return !m_connection.isEmpty() && QSqlDatabase::database(m_connection, false).isOpen(); }

void ReadingLog::add(int bookId, const QDate& day, int seconds, int pages, qint64 chars)
{
    if (!isOpen() || bookId <= 0 || !day.isValid() || (seconds <= 0 && pages <= 0)) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT INTO reading_log (book_id, day, seconds, pages, chars) VALUES (?,?,?,?,?) "
                             "ON CONFLICT(book_id, day) DO UPDATE SET seconds=seconds+excluded.seconds, pages=pages+excluded.pages, "
                             "chars=chars+excluded.chars"));
    q.addBindValue(bookId);
    q.addBindValue(day.toString(Qt::ISODate));
    q.addBindValue(qMax(0, seconds));
    q.addBindValue(qMax(0, pages));
    q.addBindValue(qMax<qint64>(0, chars));
    q.exec();
}

Statistics computeStatistics(Library* library, const QDate& today, int dayCount)
{
    Statistics s;
    if (!library || !library->isOpen() || !today.isValid()) return s;
    const QSqlDatabase db = QSqlDatabase::database(library->connectionName());
    const QDate first = today.addDays(-(qMax(1, dayCount) - 1));
    QHash<QDate, DayStats> byDay;
    QSet<QDate> active;   // every day with reading or practice, all time (the streaks)
    QHash<int, BookStats> books;

    // Reading.
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT book_id, day, seconds, pages FROM reading_log"))) {
        QHash<QDate, int> secondsByDay;
        while (q.next()) {
            const int book = q.value(0).toInt();
            const QDate d = QDate::fromString(q.value(1).toString(), Qt::ISODate);
            const int sec = q.value(2).toInt(), pages = q.value(3).toInt();
            s.totalSeconds += sec;
            secondsByDay[d] += sec;
            BookStats& b = books[book];
            b.bookId = book;
            b.seconds += sec;
            b.pages += pages;
            if (d == today) s.todaySeconds += sec;
            if (d > today.addDays(-7) && d <= today) {
                s.weekSeconds += sec;
                s.pagesWeek += pages;
            }
            if (d >= first && d <= today) {
                s.monthSeconds += sec;
                byDay[d].readingSeconds += sec;
                byDay[d].pages += pages;
            }
        }
        for (auto it = secondsByDay.cbegin(); it != secondsByDay.cend(); ++it)
            if (it.value() >= 60) active.insert(it.key());
    }

    // Words.
    if (q.exec(QStringLiteral("SELECT added, COALESCE(book_id, 0) FROM words"))) {
        while (q.next()) {
            ++s.words;
            const QDate d = localDay(q.value(0).toString());
            const int book = q.value(1).toInt();
            if (book > 0) {
                books[book].bookId = book;
                ++books[book].words;
            }
            if (d > today.addDays(-7) && d <= today) ++s.wordsWeek;
            if (d >= first && d <= today) ++byDay[d].newWords;
        }
    }
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM cards WHERE state=2")) && q.next()) s.cardsLearned = q.value(0).toInt();

    // Practice.
    int recalled = 0, answered = 0;
    if (q.exec(QStringLiteral("SELECT reviewed, rating FROM reviews"))) {
        while (q.next()) {
            const QDate d = localDay(q.value(0).toString());
            if (!d.isValid()) continue;
            active.insert(d);
            if (d == today) ++s.reviewsToday;
            if (d >= first && d <= today) {
                ++s.reviewsMonth;
                ++byDay[d].reviews;
                ++answered;
                if (q.value(1).toInt() > 1) ++recalled;   // 1 = Again
            }
        }
    }
    s.retentionMonth = answered > 0 ? int(100.0 * recalled / answered + 0.5) : -1;

    // Streaks: days in a row; today counts once there's activity, and an unbroken run up to yesterday still holds.
    {
        QDate d = active.contains(today) ? today : today.addDays(-1);
        while (active.contains(d)) {
            ++s.streak;
            d = d.addDays(-1);
        }
        QList<QDate> all(active.cbegin(), active.cend());
        std::sort(all.begin(), all.end());
        int run = 0;
        for (int i = 0; i < all.size(); ++i) {
            run = i > 0 && all[i - 1].addDays(1) == all[i] ? run + 1 : 1;
            s.bestStreak = qMax(s.bestStreak, run);
        }
    }

    // Books: titles, progress, finished.
    if (q.exec(QStringLiteral("SELECT b.id, b.title, b.author, b.finished, COALESCE(p.progress, 0) FROM books b LEFT JOIN positions p ON p.book_id=b.id"))) {
        while (q.next()) {
            const int id = q.value(0).toInt();
            const bool finished = q.value(3).toInt() != 0;
            const double progress = q.value(4).toDouble();
            if (finished) ++s.booksFinished;
            else if (progress > 0.0) ++s.booksReading;
            if (!books.contains(id)) continue;
            BookStats& b = books[id];
            b.title = q.value(1).toString();
            b.author = q.value(2).toString();
            b.finished = finished;
            b.progress = finished ? 1.0 : progress;
        }
    }
    for (auto it = books.cbegin(); it != books.cend(); ++it)
        if (!it.value().title.isEmpty() || it.value().seconds > 0) s.books << it.value();
    std::sort(s.books.begin(), s.books.end(), [](const BookStats& a, const BookStats& b) {
        return a.seconds != b.seconds ? a.seconds > b.seconds : a.words > b.words;
    });

    for (QDate d = first; d <= today; d = d.addDays(1)) {
        DayStats ds = byDay.value(d);
        ds.day = d;
        s.days << ds;
    }
    return s;
}

}  // namespace core

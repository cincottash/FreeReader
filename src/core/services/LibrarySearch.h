// Search All Books: the text of every book in the library, one book after another on a background thread, the
// results of each book reported as soon as it is searched. Uses the same rules as Search This Book (case and
// accents ignored unless asked, whole words optional). Books whose file is missing or unreadable are skipped and
// reported as such.
#pragma once

#include "BookSearch.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <memory>

namespace core {

struct LibraryBookHits {
    int bookId = 0;
    QString title, author, path;
    QList<SearchHit> hits;
    bool truncated = false;   // more than the limit per book
    QString error;            // the book couldn't be read (missing file, damaged)
};

class LibrarySearch : public QObject {
    Q_OBJECT
public:
    struct BookRef {
        int id = 0;
        QString path, title, author;
    };
    explicit LibrarySearch(QObject* parent = nullptr);
    ~LibrarySearch() override;

    // Starts a search (cancelling one that is running); returns its id.
    int start(const QList<BookRef>& books, const QString& query, const SearchOptions& options, int perBookLimit = 100);
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    // Every book, in order, hits or not (`done` of `total` searched so far).
    void bookSearched(int id, const core::LibraryBookHits& result, int done, int total);
    void finished(int id, int totalHits, int booksWithHits, bool canceled);

private:
    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    int m_id = 0;
    bool m_running = false;
};

}  // namespace core

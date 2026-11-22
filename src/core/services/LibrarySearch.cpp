#include "LibrarySearch.h"

#include "core/format/FormatPlugin.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

namespace core {

LibrarySearch::LibrarySearch(QObject* parent) : QObject(parent) { m_pool.setMaxThreadCount(1); }

LibrarySearch::~LibrarySearch()
{
    cancel();
    m_pool.waitForDone();
}

void LibrarySearch::cancel()
{
    if (m_cancel) m_cancel->store(true);
}

int LibrarySearch::start(const QList<BookRef>& books, const QString& query, const SearchOptions& options, int perBookLimit)
{
    cancel();
    const int id = ++m_id;
    auto stop = std::make_shared<std::atomic<bool>>(false);
    m_cancel = stop;
    m_running = true;
    QPointer<LibrarySearch> self(this);
    SearchOptions o = options;
    o.limit = perBookLimit;
    m_pool.start([self, books, query, o, stop, id] {
        int total = 0, withHits = 0;
        for (int i = 0; i < books.size(); ++i) {
            if (stop->load()) break;
            const BookRef& b = books[i];
            LibraryBookHits r;
            r.bookId = b.id;
            r.title = b.title;
            r.author = b.author;
            r.path = b.path;
            if (!QFileInfo(b.path).isFile()) {
                r.error = QObject::tr("The book's file wasn't found.");
            } else {
                QString error;
                const std::shared_ptr<Book> book = FormatRegistry::instance().open(b.path, &error);
                if (!book) r.error = error.isEmpty() ? QObject::tr("The book can't be read.") : error;
                else r.hits = searchBook(*book, query, o, stop.get(), &r.truncated);
            }
            if (stop->load()) break;
            total += int(r.hits.size());
            if (!r.hits.isEmpty()) ++withHits;
            const int done = i + 1, count = int(books.size());
            QMetaObject::invokeMethod(self, [self, id, r, done, count, stop] {
                if (self && !stop->load() && id == self->m_id) emit self->bookSearched(id, r, done, count);
            });
        }
        const bool canceled = stop->load();
        QMetaObject::invokeMethod(self, [self, id, total, withHits, canceled] {
            if (!self || id != self->m_id) return;
            self->m_running = false;
            emit self->finished(id, total, withHits, canceled);
        });
    });
    return id;
}

}  // namespace core

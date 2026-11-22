// The book engine: opens a book through its format plug-in, builds its index, and lays out chapters on background
// threads. Laid-out chapters are cached per layout parameters; a change of font, size, columns or window size
// starts a new generation and results of the old one are dropped. Lives on the UI thread; results arrive as
// signals.
#pragma once

#include "BookIndex.h"
#include "BookSearch.h"

#include "core/format/Book.h"
#include "core/layout/Layout.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include <atomic>

#include <memory>

namespace layout {
class FontResolver;
}

namespace core {

struct NoteText {
    QString text;          // the note's text, paragraphs separated by blank lines
    bool isNote = false;   // the target is marked as a footnote/endnote
};

class BookEngine : public QObject {
    Q_OBJECT
public:
    explicit BookEngine(QObject* parent = nullptr);
    ~BookEngine() override;

    void open(const QString& path);   // asynchronous: opened() or openFailed()
    void close();
    bool isOpen() const { return m_book != nullptr; }
    const Book* book() const { return m_book.get(); }
    std::shared_ptr<const BookIndex> index() const { return m_index; }   // null until indexReady()

    void setParams(const layout::LayoutParams& p);
    const layout::LayoutParams& params() const { return m_params; }
    void setBodyFont(const QStringList& families);

    // The chapter's layout for the current parameters, or null (then it is requested; layoutReady follows).
    std::shared_ptr<const layout::ChapterLayout> layout(int spine);
    void request(int spine);
    // Waits for the chapter (tests and the screenshot mode only).
    std::shared_ptr<const layout::ChapterLayout> layoutNow(int spine);

    QString cfiFor(const Position& p);                 // needs the chapter's flow (built if missing)
    bool positionForCfi(const QString& cfi, Position* out);
    NoteText noteText(const QString& href) const;      // the text a footnote link points to
    QByteArray resource(const QString& path) const;

    // Searches the whole book on a background thread; searchFinished reports it. A new search cancels the last.
    int search(const QString& query, const SearchOptions& options);
    void cancelSearch();

signals:
    void opened();
    void openFailed(const QString& message);
    void indexReady();
    void layoutReady(int spine);
    void searchFinished(int id, const QList<core::SearchHit>& hits, bool truncated);

private:
    std::shared_ptr<const layout::ChapterFlow> flowFor(int spine);   // from the cache or built now
    void store(int spine, int generation, std::shared_ptr<const layout::ChapterLayout> cl);

    QThreadPool m_pool;
    QThreadPool m_searchPool;   // one search at a time, waited for on close
    std::shared_ptr<Book> m_book;
    std::shared_ptr<const BookIndex> m_index;
    std::shared_ptr<layout::FontResolver> m_fonts;
    QStringList m_bodyFamilies;
    layout::LayoutParams m_params;
    int m_generation = 0;
    int m_openToken = 0;
    QHash<int, std::shared_ptr<const layout::ChapterLayout>> m_cache;
    QList<int> m_recent;   // most recently used first
    QSet<int> m_pending;
    int m_searchId = 0;
    std::shared_ptr<std::atomic<bool>> m_searchCancel;
};

}  // namespace core

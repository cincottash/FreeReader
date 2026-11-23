// What the reader needs to know about the whole book, built once in the background after opening: the length
// of every chapter (for progress and stable page numbers), where each table of contents entry and each element
// id is, and the publisher's page numbers when the book has them.
#pragma once

#include "core/position/Position.h"

#include <QHash>
#include <QString>

#include <memory>
#include <vector>

namespace core {

class Book;

// Stable page numbers: one page per this many characters of the book's text, so a page number never changes
// with the font size or the window. Books with a publisher page list use their printed page numbers instead.
constexpr int kCharsPerPage = 1800;

struct BookIndex {
    std::vector<int> chapterLength;          // per spine item
    std::vector<qint64> chapterStart;        // per spine item, in the whole book's text
    qint64 total = 0;
    std::vector<QHash<QString, int>> ids;    // per spine item: element id -> offset

    struct TocPosition {
        int spine = -1;                      // -1: the entry's target couldn't be found
        int offset = 0;
        qint64 global = 0;
    };
    std::vector<TocPosition> toc;            // per Book::toc entry

    struct PrintedPage {
        qint64 global = 0;
        QString label;
    };
    std::vector<PrintedPage> printedPages;   // sorted; empty when the book has no usable page list

    qint64 global(const Position& p) const;
    Position fromGlobal(qint64 g) const;
    double fraction(const Position& p) const { return total > 0 ? double(global(p)) / double(total) : 0.0; }
    // Resolves "OEBPS/ch2.xhtml#sec3" (or a bare path) to a position.
    bool positionFor(const Book& book, const QString& href, Position* out) const;

    // "31" and "1,072" style page numbers (labels from the book's page list, or stable synthetic pages).
    QString pageLabel(const Position& p) const;
    int pageCount() const;
    int pageNumber(const Position& p) const;   // 1-based, in the numbering pageLabel() shows (printed or synthetic)
    int pageNumberAt(qint64 global) const;
    QString lastPageLabel() const;
    Position positionOfPage(int pageNumber) const;   // synthetic or printed page, 1-based
    int tocEntryAt(const Position& p) const;   // the deepest entry the position is in, -1 before the first
};

std::shared_ptr<BookIndex> buildIndex(const Book& book);

// What is left of the chapter at `p`: the time ("6 min left in chapter", from `readingSpeed` characters a minute)
// or the pages ("3 pages left in chapter", in the numbering pageLabel() shows).
QString chapterLeftText(const BookIndex& index, const Position& p, int readingSpeed, bool pages);

}  // namespace core

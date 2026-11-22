// The layout engine: a chapter's flow laid out into lines and pages for one set of layout parameters. Runs on a
// background thread; the result is read-only and drawn by the page renderer on the UI thread.
#pragma once

#include "Flow.h"

#include <QImage>
#include <QRecursiveMutex>
#include <QRectF>
#include <QTextLayout>

#include <memory>
#include <vector>

namespace core {
class Book;
}

namespace layout {

struct LayoutParams {
    float columnWidth = 600;       // px of one column (page)
    float pageHeight = 800;        // px of text area
    float pxPerCssPx = 1.25f;      // the reader's text size
    float lineSpacing = 1.5f;      // the reader's line spacing (minimum; a book's taller spacing is kept)
    css::Align defaultAlign = css::Align::Justify;   // paragraphs the book doesn't align
    bool justify = true;           // off: the book's justified paragraphs are set ragged-right too
    bool hyphenate = false;
    QString fontKey;               // the reader's font choice (part of the cache key)
    qreal devicePixelRatio = 1;

    bool operator==(const LayoutParams&) const = default;
};

// One text block laid out at a width: its lines, each with the top of its slot and the slot's height.
//
// A layout made on a background thread holds that thread's font engines, which must not be used from the UI
// thread. So the background thread keeps only the results (line tops and heights, page breaks) and drops its
// QTextLayout; ui() rebuilds the same lines on the UI thread when a page is drawn or hit-tested.
struct TextLines {
    mutable std::unique_ptr<QTextLayout> layout;
    std::vector<float> top, advance;
    float height = 0;
    float left = 0;                // where the block's lines start (its left margin), for the list marker
    const Block* block = nullptr;
    float width = 0;
    LayoutParams params;
    // With hyphenation the laid-out text has extra soft hyphens. toLayout maps a block index to a layout index,
    // toFlow the other way (both empty: the same text).
    std::vector<int> toLayout, toFlow;
    int L(int blockIndex) const { return toLayout.empty() ? blockIndex : toLayout[size_t(qBound(0, blockIndex, int(toLayout.size()) - 1))]; }
    int F(int layoutIndex) const { return toFlow.empty() ? layoutIndex : toFlow[size_t(qBound(0, layoutIndex, int(toFlow.size()) - 1))]; }

    const QTextLayout& ui() const; // UI thread only
    void release() { layout.reset(); }
};

struct TableLayout;

// Blocks laid out without page breaks (inside a table cell).
struct BoxLayout {
    struct Item {
        const Block* block = nullptr;
        float y = 0;
        TextLines text;
        QImage image;
        QRectF rect;               // image, rule or nested table, relative to the box
        std::unique_ptr<TableLayout> table;
    };
    std::vector<Item> items;
    float height = 0;
};

struct TableLayout {
    struct Cell {
        float x = 0, width = 0;    // relative to the table's left edge
        bool header = false;
        BoxLayout box;
    };
    struct Row {
        float height = 0;
        std::vector<Cell> cells;
    };
    std::vector<Row> rows;
    std::vector<int> rowStart, rowEnd;   // chapter offsets covered by each row
    std::vector<bool> headerRow;         // every cell is a header cell
    float width = 0;
    bool borders = false;
    float padX = 6, padY = 3;
};

struct PageItem {
    enum Kind : quint8 { Text, Image, Rule, TableRow };
    Kind kind = Text;
    int block = 0;                 // index in the flow's blocks
    int firstLine = 0, lineCount = 0;
    int row = 0;
    float dy = 0;                  // Text: add to a line's own y to get its y on the page
    QRectF rect;                   // Image, Rule, TableRow: on the page; Text: the lines' bounds
};

struct Page {
    std::vector<PageItem> items;
    int start = 0, end = 0;        // chapter offsets of the first and after the last character on the page
};

struct ChapterLayout {
    int spine = -1;
    LayoutParams params;
    std::shared_ptr<const ChapterFlow> flow;
    std::vector<TextLines> text;                       // per flow block
    std::vector<std::unique_ptr<TableLayout>> tables;  // per flow block
    std::vector<QImage> images;                        // per flow block, scaled for the screen
    std::vector<Page> pages;

    int pageForOffset(int offset) const;               // the page holding this offset (the last one before it)
};

std::shared_ptr<ChapterLayout> layoutChapter(const std::shared_ptr<const ChapterFlow>& flow, const core::Book& book, int spine,
                                             const LayoutParams& params);

// Qt's font engines aren't thread-safe, and a layout made on a background thread keeps using that thread's
// engines when it is drawn. Everything that shapes text (layout, on any thread) or draws/measures a laid-out
// page (UI thread) holds this lock, one paragraph or one page at a time, so the two never overlap.
QRecursiveMutex& fontEngineLock();

// Shared with the renderer and hit testing.
TextLines layoutText(const Block& b, float width, const LayoutParams& p);
QTextOption::WrapMode wrapMode();

}  // namespace layout

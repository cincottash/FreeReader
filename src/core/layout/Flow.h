// A chapter as a flow of blocks (paragraphs with their formatting, images, rules, tables), built from the chapter's
// XHTML and CSS. Every character has a chapter offset that depends only on the book's text, never on fonts or
// window size: positions, highlights and page numbers are built on these offsets.
//
// Offsets: the characters of each text block, then one extra offset for the block boundary. An image or rule
// counts as one character plus the boundary. A table's cells continue the count in reading order.
#pragma once

#include "core/css/Style.h"

#include <QFont>
#include <QHash>
#include <QImage>
#include <QString>
#include <QTextLayout>

#include <memory>
#include <vector>

namespace core {
class Book;
}

namespace layout {

class FontResolver;

struct LinkSpan {
    int start = 0;        // index in the block's text
    int end = 0;
    QString target;       // "OEBPS/notes.xhtml#n1", or an external URL
    bool external = false;
    bool noteRef = false; // looks like a footnote reference (epub:type="noteref", a short mark like "1" or "*")
};

struct Table;

// A small image inside a line of text (an icon, a decorated letter). It takes one character (U+2007, widened to
// the image's width) and is drawn on that line's baseline.
struct InlineImage {
    int index = 0;        // in the block's text
    QSizeF size;          // px
    QImage image;
    QString path;
};

struct Block {
    enum Kind : quint8 { Text, Image, Rule, Table };
    Kind kind = Text;
    int start = 0;
    int length = 0;

    // Text
    QString text;                              // U+2028 marks a line break (<br>)
    QList<QTextLayout::FormatRange> formats;
    std::vector<LinkSpan> links;
    std::vector<InlineImage> inlineImages;
    QString marker;                           // list marker, drawn left of the first line
    css::Align align = css::Align::Start;
    float indent = 0;                          // first line, px (negative = hanging)
    float lineHeight = 0;                      // the book's multiplier (0 = normal)
    QFont font;                                // the paragraph's base font
    QString lang;                              // the text's language ("es"), for hyphenation
    bool hyphenate = true;                     // false where the book says hyphens: none/manual
    float fontPx = 16;

    // Box (px at the reader's text size; percentages of the column width kept apart)
    float spaceBefore = 0;
    float marginLeft = 0, marginRight = 0;
    float marginLeftPct = 0, marginRightPct = 0;
    bool breakBefore = false;
    bool keepWithNext = false;
    bool avoidInside = false;
    bool chapterStart = false;

    // Image
    QString imagePath;
    QString alt;
    css::Len width{0, css::Len::Auto}, height{0, css::Len::Auto};

    // Table
    std::shared_ptr<layout::Table> table;
};

struct TableCell {
    std::vector<Block> blocks;
    int colspan = 1;
    bool header = false;
};

struct TableRow {
    std::vector<TableCell> cells;
};

struct Table {
    std::vector<TableRow> rows;
    bool borders = false;
    float cellPadX = 6, cellPadY = 3;          // px
};

// One text node of the chapter, for EPUB CFI positions.
struct TextNodeRef {
    QString path;          // CFI steps inside the chapter document: "/4/2[ch1]/10/1"
    int start = 0;         // chapter offset of its first character in the flow
    int length = 0;        // characters it produced in the flow
    QString raw;           // the node's text as in the file
    bool lastSpace = true, pendingSpace = false;   // whitespace collapsing state when the node began
    css::WhiteSpace whiteSpace = css::WhiteSpace::Normal;
};

struct ChapterFlow {
    std::vector<Block> blocks;
    int length = 0;                            // offsets used (the end of the chapter)
    QHash<QString, int> ids;                   // element id -> offset
    std::vector<TextNodeRef> textNodes;        // in offset order
    QString plainText() const;                 // for tests and copying
    QString textRange(int from, int to) const; // block boundaries become line breaks
};

struct FlowOptions {
    float pxPerCssPx = 1.25f;                  // the reader's text size: 20 px for the book's 16 px "medium"
    const FontResolver* fonts = nullptr;
};

// Builds the flow of one spine item. Never fails: a chapter that can't be read becomes an empty flow.
std::shared_ptr<ChapterFlow> buildFlow(const core::Book& book, int spineIndex, const FlowOptions& options);

// Chapter offset <-> (text node, offset in its raw text), for EPUB CFI.
bool rawOffsetFor(const ChapterFlow& flow, int offset, const TextNodeRef** node, int* rawOffset);
int flowOffsetFor(const TextNodeRef& node, int rawOffset);

}  // namespace layout

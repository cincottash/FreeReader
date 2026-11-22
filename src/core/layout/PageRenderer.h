// Drawing laid-out pages, and the reverse: from a point on a page to a character. Selections and highlights are
// overlays painted from chapter offsets, never baked into the layout.
#pragma once

#include "Layout.h"

#include <QColor>
#include <QPointF>
#include <QRectF>

#include <vector>

class QPainter;

namespace layout {

struct RenderColors {
    QColor text, muted, accent, line, surface;
};

struct Overlay {
    int start = 0, end = 0;   // chapter offsets
    QColor color;
};

// Paints one page with its top-left corner at `origin`.
void paintPage(QPainter& p, const ChapterLayout& cl, int page, const QPointF& origin, const RenderColors& colors,
               const std::vector<Overlay>& overlays);

struct Hit {
    int offset = -1;            // nearest character position (between characters)
    int charOffset = -1;        // the character under the point, -1 if the point isn't on text
    const LinkSpan* link = nullptr;
    int linkBlockStart = 0;     // chapter offset of the link's block
    bool image = false;
    QString imagePath;
};

// `pos` is relative to the page's top-left corner.
Hit hitTest(const ChapterLayout& cl, int page, const QPointF& pos);

// Rectangles (page coordinates) covering the characters [start, end) on this page.
std::vector<QRectF> rangeRects(const ChapterLayout& cl, int page, int start, int end);

}  // namespace layout

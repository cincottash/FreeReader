#include "PageRenderer.h"

#include <QFontMetricsF>
#include <QGlyphRun>
#include <QPainter>
#include <QRawFont>
#include <QPainterPath>

#include <cmath>
#include <functional>
#include <limits>

namespace layout {

namespace {

struct LineRef {
    const Block* block;
    const TextLines* lines;
    int line;
    QPointF off;     // add to the line's own position to get page coordinates
    float slotTop;   // page coordinates
    float slotHeight;
};

struct ImageRef {
    const Block* block;
    const QImage* image;
    QRectF rect;     // page coordinates
};

struct Visitor {
    std::function<void(const LineRef&)> line;
    std::function<void(const ImageRef&)> image;
    std::function<void(const QRectF&)> rule;
    std::function<void(const TableLayout&, int row, const QRectF& rowRect)> tableRow;
};

void visitBox(const BoxLayout& box, const QPointF& origin, const Visitor& v);

void visitTableRow(const TableLayout& t, int r, const QRectF& rowRect, const Visitor& v)
{
    if (v.tableRow) v.tableRow(t, r, rowRect);
    const auto& row = t.rows[size_t(r)];
    for (const auto& cell : row.cells)
        visitBox(cell.box, QPointF(rowRect.x() + cell.x + t.padX, rowRect.y() + t.padY), v);
}

void visitBox(const BoxLayout& box, const QPointF& origin, const Visitor& v)
{
    for (const auto& item : box.items) {
        const Block& b = *item.block;
        if (b.kind == Block::Text && item.text.block) {
            const int uiLines = item.text.ui().lineCount();
            for (int li = 0; li < int(item.text.advance.size()) && li < uiLines; ++li) {
                if (v.line)
                    v.line({&b, &item.text, li, QPointF(origin.x(), origin.y() + item.y), float(origin.y() + item.y + item.text.top[size_t(li)]),
                            item.text.advance[size_t(li)]});
            }
        } else if (b.kind == Block::Image) {
            if (v.image) v.image({&b, &item.image, item.rect.translated(origin)});
        } else if (b.kind == Block::Rule) {
            if (v.rule) v.rule(item.rect.translated(origin));
        } else if (b.kind == Block::Table && item.table) {
            float y = float(origin.y() + item.rect.y());
            for (int r = 0; r < int(item.table->rows.size()); ++r) {
                const QRectF rowRect(origin.x() + item.rect.x(), y, item.table->width, item.table->rows[size_t(r)].height);
                visitTableRow(*item.table, r, rowRect, v);
                y += item.table->rows[size_t(r)].height;
            }
        }
    }
}

void visitPage(const ChapterLayout& cl, int page, const Visitor& v)
{
    if (page < 0 || page >= int(cl.pages.size())) return;
    for (const PageItem& it : cl.pages[size_t(page)].items) {
        const Block& b = cl.flow->blocks[size_t(it.block)];
        switch (it.kind) {
        case PageItem::Text: {
            const TextLines& tl = cl.text[size_t(it.block)];
            const int uiLines = tl.ui().lineCount();
            for (int li = it.firstLine; li < it.firstLine + it.lineCount && li < uiLines; ++li)
                if (v.line) v.line({&b, &tl, li, QPointF(0, it.dy), tl.top[size_t(li)] + it.dy, tl.advance[size_t(li)]});
            break;
        }
        case PageItem::Image:
            if (v.image) v.image({&b, &cl.images[size_t(it.block)], it.rect});
            break;
        case PageItem::Rule:
            if (v.rule) v.rule(it.rect);
            break;
        case PageItem::TableRow:
            if (const TableLayout* t = cl.tables[size_t(it.block)].get()) visitTableRow(*t, it.row, it.rect, v);
            break;
        }
    }
}

// The part of [start, end) (chapter offsets) on one line, as an x range in page coordinates.
bool lineSpan(const LineRef& r, int start, int end, qreal* x1, qreal* x2)
{
    const QTextLine line = r.lines->ui().lineAt(r.line);
    // In the book's offsets; the line's own positions may include inserted soft hyphens.
    const int ls = r.lines->F(line.textStart()), le = r.lines->F(line.textStart() + line.textLength());
    const int s = qMax(start - r.block->start, ls), e = qMin(end - r.block->start, le);
    if (e <= s) return false;
    *x1 = line.cursorToX(r.lines->L(s)) + r.off.x();
    *x2 = line.cursorToX(r.lines->L(e)) + r.off.x();
    if (*x2 < *x1) std::swap(*x1, *x2);
    // A selection that continues on the next line reaches the end of this one.
    if (end - r.block->start > le && e == le) *x2 = qMax(*x2, line.x() + line.naturalTextWidth() + r.off.x());
    return true;
}

// The height of a selection or highlight on a line: the line's slot, unless bigger text elsewhere on the line (a
// drop cap at 300 %) made it taller than the selected words need. Then a normal line's height, around the selected
// words (the owner's report, 2026-11-27: "señora" highlighted far above the word).
void spanHeight(const LineRef& r, int start, int end, qreal* top, qreal* height)
{
    *top = r.slotTop;
    *height = r.slotHeight;
    const QTextLine line = r.lines->ui().lineAt(r.line);
    const int ls = r.lines->F(line.textStart()), le = r.lines->F(line.textStart() + line.textLength());
    const int s = qMax(start - r.block->start, ls), e = qMin(end - r.block->start, le);
    if (e <= s) return;
    const int from = r.lines->L(s), to = r.lines->L(e);
    qreal ascent = 0, descent = 0;
    for (const QGlyphRun& run : line.glyphRuns(from, to - from)) {
        ascent = qMax(ascent, run.rawFont().ascent());
        descent = qMax(descent, run.rawFont().descent());
    }
    if (ascent <= 0) return;
    const float spacing = qMax(r.lines->params.lineSpacing, r.block->lineHeight);
    const qreal normal = std::ceil(qMax<qreal>(ascent + descent, r.block->fontPx * spacing));
    if (normal >= r.slotHeight) return;   // the line is no taller than these words need: its whole slot
    const qreal baseline = r.off.y() + line.y() + line.ascent();
    const qreal middle = baseline + (descent - ascent) / 2;
    *top = qBound<qreal>(r.slotTop, middle - normal / 2, r.slotTop + r.slotHeight - normal);
    *height = normal;
}

}  // namespace

void paintPage(QPainter& p, const ChapterLayout& cl, int page, const QPointF& origin, const RenderColors& colors,
               const std::vector<Overlay>& overlays)
{
    QMutexLocker lock(&fontEngineLock());
    p.save();
    p.translate(origin);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 1. Overlays (selection, highlights) behind the text.
    if (!overlays.empty()) {
        Visitor ov;
        ov.line = [&](const LineRef& r) {
            for (const Overlay& o : overlays) {
                qreal x1, x2;
                if (!lineSpan(r, o.start, o.end, &x1, &x2)) continue;
                qreal top, height;
                spanHeight(r, o.start, o.end, &top, &height);
                QPainterPath path;
                path.addRoundedRect(QRectF(x1 - 1, top + 1, x2 - x1 + 2, height - 2), 3, 3);
                p.fillPath(path, o.color);
            }
        };
        visitPage(cl, page, ov);
    }

    // 2. Table cells (header tint and borders) under their text.
    Visitor bg;
    bg.tableRow = [&](const TableLayout& t, int r, const QRectF& rowRect) {
        const auto& row = t.rows[size_t(r)];
        for (const auto& cell : row.cells) {
            const QRectF cr(rowRect.x() + cell.x, rowRect.y(), cell.width, rowRect.height());
            if (cell.header) {
                QColor tint = colors.line;
                tint.setAlphaF(0.35f);
                p.fillRect(cr, tint);
            }
            if (t.borders) {
                p.setPen(QPen(colors.line, 1));
                p.setBrush(Qt::NoBrush);
                p.drawRect(cr.adjusted(0.5, 0.5, -0.5, -0.5));
            }
        }
        if (!t.borders) {
            // Without borders, a faint rule between rows keeps rows readable.
            p.setPen(QPen(colors.line, 1));
            p.drawLine(QPointF(rowRect.left(), rowRect.bottom() - 0.5), QPointF(rowRect.right(), rowRect.bottom() - 0.5));
        }
    };
    visitPage(cl, page, bg);

    // 3. Text, list markers and link underlines.
    p.setPen(colors.text);
    Visitor fg;
    fg.line = [&](const LineRef& r) {
        const QTextLine line = r.lines->ui().lineAt(r.line);
        p.setPen(colors.text);
        line.draw(&p, r.off);
        for (const InlineImage& im : r.block->inlineImages) {
            const int at = r.lines->L(im.index);
            if (at < line.textStart() || at >= line.textStart() + line.textLength() || im.image.isNull()) continue;
            const qreal x = line.cursorToX(at) + r.off.x() + 1;
            const qreal baseline = r.off.y() + line.y() + line.ascent();
            p.drawImage(QRectF(x, baseline - im.size.height(), im.size.width(), im.size.height()), im.image);
        }
        if (r.line == 0 && !r.block->marker.isEmpty()) {
            const QFontMetricsF fm(r.block->font);
            const qreal w = fm.horizontalAdvance(r.block->marker);
            p.setFont(r.block->font);
            p.drawText(QPointF(r.off.x() + r.lines->left - w - fm.averageCharWidth() * 0.8, r.off.y() + line.y() + line.ascent()), r.block->marker);
        }
        for (const LinkSpan& l : r.block->links) {
            qreal x1, x2;
            if (!lineSpan(r, r.block->start + l.start, r.block->start + l.end, &x1, &x2)) continue;
            QColor c = colors.accent;
            c.setAlphaF(0.75f);
            const qreal y = std::round(r.off.y() + line.y() + line.ascent() + qMax<qreal>(2, line.descent() * 0.45)) + 0.5;
            p.setPen(QPen(c, 1, l.noteRef ? Qt::SolidLine : Qt::DotLine));
            p.drawLine(QPointF(x1, y), QPointF(x2, y));
        }
    };
    fg.image = [&](const ImageRef& r) {
        if (r.image && !r.image->isNull()) {
            p.drawImage(r.rect, *r.image);
        } else {
            p.setPen(QPen(colors.line, 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.rect.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
            p.setPen(colors.muted);
            p.drawText(r.rect, Qt::AlignCenter | Qt::TextWordWrap, r.block->alt.isEmpty() ? QStringLiteral("Image") : r.block->alt);
        }
    };
    fg.rule = [&](const QRectF& rect) { p.fillRect(rect, colors.line); };
    visitPage(cl, page, fg);
    p.restore();
}

Hit hitTest(const ChapterLayout& cl, int page, const QPointF& pos)
{
    QMutexLocker lock(&fontEngineLock());
    Hit hit;
    // Images first.
    Visitor vi;
    vi.image = [&](const ImageRef& r) {
        if (r.rect.contains(pos)) {
            hit.image = true;
            hit.imagePath = r.block->imagePath;
            hit.offset = r.block->start;
            hit.charOffset = r.block->start;
        }
    };
    visitPage(cl, page, vi);
    if (hit.image) return hit;

    const LineRef* best = nullptr;
    LineRef bestCopy{};
    qreal bestDist = std::numeric_limits<qreal>::max();
    Visitor vl;
    vl.line = [&](const LineRef& r) {
        const QTextLine line = r.lines->ui().lineAt(r.line);
        const qreal top = r.slotTop, bottom = r.slotTop + r.slotHeight;
        qreal dy = pos.y() < top ? top - pos.y() : pos.y() >= bottom ? pos.y() - bottom + 0.01 : 0;
        const qreal left = line.x() + r.off.x(), right = left + line.width();
        const qreal dx = pos.x() < left ? left - pos.x() : pos.x() > right ? pos.x() - right : 0;
        const qreal d = dy * 4 + dx;   // vertical distance matters most
        if (d < bestDist) {
            bestDist = d;
            bestCopy = r;
            best = &bestCopy;
        }
    };
    visitPage(cl, page, vl);
    if (!best) return hit;
    const QTextLine line = best->lines->ui().lineAt(best->line);
    const qreal x = pos.x() - best->off.x();
    const int cursor = best->lines->F(line.xToCursor(x, QTextLine::CursorBetweenCharacters));
    hit.offset = best->block->start + cursor;
    const bool onText = bestDist == 0 && x >= line.x() && x <= line.x() + line.naturalTextWidth();
    if (onText) {
        const int c = best->lines->F(qBound(line.textStart(), line.xToCursor(x, QTextLine::CursorOnCharacter), line.textStart() + qMax(0, line.textLength() - 1)));
        hit.charOffset = best->block->start + c;
        for (const InlineImage& im : best->block->inlineImages)
            if (im.index == c) {
                hit.image = true;
                hit.imagePath = im.path;
            }
        for (const LinkSpan& l : best->block->links)
            if (c >= l.start && c < l.end) {
                hit.link = &l;
                hit.linkBlockStart = best->block->start;
            }
    }
    return hit;
}

std::vector<QRectF> rangeRects(const ChapterLayout& cl, int page, int start, int end)
{
    QMutexLocker lock(&fontEngineLock());
    std::vector<QRectF> out;
    Visitor v;
    v.line = [&](const LineRef& r) {
        qreal x1, x2;
        if (lineSpan(r, start, end, &x1, &x2)) {
            qreal top, height;
            spanHeight(r, start, end, &top, &height);
            out.emplace_back(x1, top, x2 - x1, height);
        }
    };
    v.image = [&](const ImageRef& r) {
        if (r.block->start >= start && r.block->start < end) out.push_back(r.rect);
    };
    visitPage(cl, page, v);
    return out;
}

}  // namespace layout

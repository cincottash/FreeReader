#include "Layout.h"

#include "core/format/Book.h"

#include <QBuffer>
#include <QImageReader>

#include <algorithm>
#include <cmath>

namespace layout {

namespace {

constexpr float kEpsilon = 0.5f;

Qt::Alignment qtAlign(css::Align a, css::Align fallback)
{
    if (a == css::Align::Start) a = fallback;
    switch (a) {
    case css::Align::Left: return Qt::AlignLeft;
    case css::Align::Right: return Qt::AlignRight;
    case css::Align::Center: return Qt::AlignHCenter;
    case css::Align::Justify: return Qt::AlignJustify;
    case css::Align::Start: return Qt::AlignLeft;
    }
    return Qt::AlignLeft;
}

float blockLeft(const Block& b, float width) { return b.marginLeft + b.marginLeftPct * width / 100.f; }
float blockRight(const Block& b, float width) { return b.marginRight + b.marginRightPct * width / 100.f; }

QImage loadImage(const core::Book& book, const QString& path, QSize* natural)
{
    bool ok = false;
    const QByteArray data = book.resource(path, &ok);
    if (!ok) return {};
    QBuffer buf;
    buf.setData(data);
    buf.open(QIODevice::ReadOnly);
    QImageReader r(&buf);
    r.setAutoTransform(true);
    if (natural) *natural = r.size();
    return r.read();
}

// The display size of an image block inside a column.
QSizeF imageSize(const Block& b, QSize natural, float colWidth, float pageHeight, const LayoutParams& p, bool fillPage)
{
    const float avail = qMax(20.f, colWidth - blockLeft(b, colWidth) - blockRight(b, colWidth));
    if (natural.isEmpty()) natural = QSize(int(avail), int(avail * 0.6f));
    float w, h;
    const float aspect = float(natural.height()) / float(qMax(1, natural.width()));
    if (b.width.unit == css::Len::Percent) w = avail * b.width.value / 100.f;
    else if (b.width.unit == css::Len::Px) w = b.width.value;
    else if (b.height.unit == css::Len::Px) w = b.height.value / aspect;
    else w = natural.width() * p.pxPerCssPx;
    if (b.height.unit == css::Len::Px && b.width.unit != css::Len::Auto) h = b.height.value;
    else h = w * aspect;
    if (fillPage) {
        const float k = qMin(avail / w, pageHeight / h);
        w *= k;
        h *= k;
    }
    if (w > avail) {
        h *= avail / w;
        w = avail;
    }
    const float maxH = pageHeight * (fillPage ? 1.f : 0.95f);
    if (h > maxH) {
        w *= maxH / h;
        h = maxH;
    }
    return {qMax(1.f, w), qMax(1.f, h)};
}

QImage scaledFor(const QImage& img, QSizeF size, qreal dpr)
{
    if (img.isNull()) return img;
    QImage s = img.scaled(QSize(qMax(1, qRound(size.width() * dpr)), qMax(1, qRound(size.height() * dpr))), Qt::IgnoreAspectRatio,
                          Qt::SmoothTransformation);
    s.setDevicePixelRatio(dpr);
    return s;
}

// Width measurements of a cell's content, for automatic table column widths.
void measure(const std::vector<Block>& blocks, const LayoutParams& p, float* minW, float* maxW)
{
    QMutexLocker lock(&fontEngineLock());
    *minW = 0;
    *maxW = 0;
    for (const Block& b : blocks) {
        if (b.kind == Block::Text) {
            for (float probe : {1.f, 100000.f}) {
                QTextLayout tl(b.text, b.font);
                tl.setFormats(b.formats);
                QTextOption o;
                o.setWrapMode(QTextOption::WordWrap);
                tl.setTextOption(o);
                tl.beginLayout();
                float widest = 0;
                for (QTextLine line = tl.createLine(); line.isValid(); line = tl.createLine()) {
                    line.setLineWidth(probe);
                    widest = qMax(widest, float(line.naturalTextWidth()));
                }
                tl.endLayout();
                const float extra = b.marginLeft + b.marginRight + qMax(0.f, b.indent);
                if (probe < 2) *minW = qMax(*minW, widest + extra);
                else *maxW = qMax(*maxW, widest + extra);
            }
        } else if (b.kind == Block::Table && b.table) {
            *minW = qMax(*minW, 60.f);
            *maxW = qMax(*maxW, 300.f);
        } else if (b.kind == Block::Image) {
            *minW = qMax(*minW, 24.f);
            *maxW = qMax(*maxW, 120.f);
        }
    }
    *maxW = qMax(*maxW, *minW);
}

std::unique_ptr<TableLayout> layoutTable(const Block& tb, const core::Book& book, float width, const LayoutParams& p);

// The first and after-last chapter offsets of some blocks (-1 when they hold none).
void offsetRange(const std::vector<Block>& blocks, int* first, int* end)
{
    for (const Block& b : blocks) {
        if (*first < 0 || b.start < *first) *first = b.start;
        *end = qMax(*end, b.start + b.length);
    }
}

BoxLayout layoutBox(const std::vector<Block>& blocks, const core::Book& book, float width, const LayoutParams& p)
{
    BoxLayout box;
    float y = 0;
    bool first = true;
    for (const Block& b : blocks) {
        if (!first) y += b.spaceBefore;
        first = false;
        BoxLayout::Item item;
        item.block = &b;
        item.y = y;
        switch (b.kind) {
        case Block::Text:
            item.text = layoutText(b, width, p);
            y += item.text.height;
            break;
        case Block::Image: {
            QSize natural;
            const QImage img = loadImage(book, b.imagePath, &natural);
            const QSizeF s = imageSize(b, natural, width, 100000.f, p, false);
            item.rect = QRectF((width - s.width()) / 2, y, s.width(), s.height());
            item.image = scaledFor(img, s, p.devicePixelRatio);
            y += s.height();
            break;
        }
        case Block::Rule:
            item.rect = QRectF(blockLeft(b, width), y + 4, width - blockLeft(b, width) - blockRight(b, width), 1);
            y += 9;
            break;
        case Block::Table:
            item.table = layoutTable(b, book, width, p);
            if (item.table) {
                float h = 0;
                for (const auto& r : item.table->rows) h += r.height;
                item.rect = QRectF(blockLeft(b, width), y, item.table->width, h);
                y += h;
            }
            break;
        }
        box.items.push_back(std::move(item));
    }
    box.height = y;
    return box;
}

std::unique_ptr<TableLayout> layoutTable(const Block& tb, const core::Book& book, float width, const LayoutParams& p)
{
    if (!tb.table || tb.table->rows.empty()) return nullptr;
    const Table& t = *tb.table;
    auto out = std::make_unique<TableLayout>();
    out->borders = t.borders;
    out->padX = t.cellPadX;
    out->padY = t.cellPadY;

    int cols = 0;
    for (const TableRow& r : t.rows) {
        int c = 0;
        for (const TableCell& cell : r.cells) c += cell.colspan;
        cols = qMax(cols, c);
    }
    if (cols == 0) return nullptr;

    std::vector<float> minW(size_t(cols), 0.f), maxW(size_t(cols), 0.f);
    for (const TableRow& r : t.rows) {
        int c = 0;
        for (const TableCell& cell : r.cells) {
            float mn, mx;
            measure(cell.blocks, p, &mn, &mx);
            mn += 2 * t.cellPadX;
            mx += 2 * t.cellPadX;
            for (int k = 0; k < cell.colspan && c + k < cols; ++k) {
                minW[size_t(c + k)] = qMax(minW[size_t(c + k)], mn / float(cell.colspan));
                maxW[size_t(c + k)] = qMax(maxW[size_t(c + k)], mx / float(cell.colspan));
            }
            c += cell.colspan;
        }
    }
    const float left = blockLeft(tb, width);
    float avail = qMax(40.f, width - left - blockRight(tb, width));
    float target = avail;
    if (tb.width.unit == css::Len::Percent) target = qMin(avail, avail * tb.width.value / 100.f);
    else if (tb.width.unit == css::Len::Px) target = qMin(avail, tb.width.value);
    float sumMin = 0, sumMax = 0;
    for (int c = 0; c < cols; ++c) {
        sumMin += minW[size_t(c)];
        sumMax += maxW[size_t(c)];
    }
    std::vector<float> w(static_cast<size_t>(cols), 0.f);
    for (int c = 0; c < cols; ++c) {
        if (sumMax <= target) {
            const bool stretch = tb.width.unit != css::Len::Auto;
            w[size_t(c)] = stretch && sumMax > 0 ? maxW[size_t(c)] * target / sumMax : maxW[size_t(c)];
        } else if (sumMin >= target) {
            w[size_t(c)] = minW[size_t(c)] * target / qMax(1.f, sumMin);
        } else {
            w[size_t(c)] = minW[size_t(c)] + (maxW[size_t(c)] - minW[size_t(c)]) * (target - sumMin) / qMax(1.f, sumMax - sumMin);
        }
    }
    std::vector<float> x(size_t(cols) + 1, 0.f);
    for (int c = 0; c < cols; ++c) x[size_t(c) + 1] = x[size_t(c)] + w[size_t(c)];
    out->width = x.back();

    // Cells read best left-aligned (a book's own alignment still applies).
    LayoutParams cellParams = p;
    cellParams.defaultAlign = css::Align::Left;
    int prevEnd = tb.start;
    for (const TableRow& r : t.rows) {
        TableLayout::Row row;
        int first = -1, last = prevEnd;
        bool allHeader = !r.cells.empty();
        for (const TableCell& cell : r.cells) {
            offsetRange(cell.blocks, &first, &last);
            allHeader = allHeader && cell.header;
        }
        out->rowStart.push_back(first < 0 ? prevEnd : first);
        out->rowEnd.push_back(qMax(last, first < 0 ? prevEnd : first));
        out->headerRow.push_back(allHeader);
        prevEnd = out->rowEnd.back();
        int c = 0;
        for (const TableCell& cell : r.cells) {
            if (c >= cols) break;
            const int end = qMin(cols, c + cell.colspan);
            TableLayout::Cell lc;
            lc.x = x[size_t(c)];
            lc.width = x[size_t(end)] - x[size_t(c)];
            lc.header = cell.header;
            lc.box = layoutBox(cell.blocks, book, qMax(10.f, lc.width - 2 * t.cellPadX), cellParams);
            row.height = qMax(row.height, lc.box.height + 2 * t.cellPadY);
            row.cells.push_back(std::move(lc));
            c = end;
        }
        row.height = qMax(row.height, 2 * t.cellPadY + 4);
        out->rows.push_back(std::move(row));
    }
    return out;
}

int textOffsetAtLine(const ChapterLayout& cl, int block, int line)
{
    const TextLines& tl = cl.text[size_t(block)];
    return cl.flow->blocks[size_t(block)].start + tl.layout->lineAt(line).textStart();
}

}  // namespace

QTextOption::WrapMode wrapMode() { return QTextOption::WrapAtWordBoundaryOrAnywhere; }

QRecursiveMutex& fontEngineLock()
{
    static QRecursiveMutex m;
    return m;
}

const QTextLayout& TextLines::ui() const
{
    if (!layout) layout = std::move(layoutText(*block, width, params).layout);
    return *layout;
}

namespace {

void releaseBox(BoxLayout& box);

void releaseTable(TableLayout& t)
{
    for (auto& r : t.rows)
        for (auto& c : r.cells) releaseBox(c.box);
}

void releaseBox(BoxLayout& box)
{
    for (auto& item : box.items) {
        item.text.release();
        if (item.table) releaseTable(*item.table);
    }
}

}  // namespace

TextLines layoutText(const Block& b, float width, const LayoutParams& p)
{
    QMutexLocker lock(&fontEngineLock());
    TextLines out;
    out.block = &b;
    out.width = width;
    out.params = p;
    out.layout = std::make_unique<QTextLayout>(b.text, b.font);
    QTextLayout& tl = *out.layout;
    tl.setFormats(b.formats);
    tl.setCacheEnabled(true);
    QTextOption o;
    o.setAlignment(qtAlign(b.align, p.defaultAlign));
    o.setWrapMode(wrapMode());
    o.setUseDesignMetrics(true);
    tl.setTextOption(o);

    const float left = blockLeft(b, width);
    const float contentW = qMax(40.f, width - left - blockRight(b, width));
    out.left = left;
    const float spacing = qMax(p.lineSpacing, b.lineHeight);
    float y = 0;
    bool first = true;
    tl.beginLayout();
    for (QTextLine line = tl.createLine(); line.isValid(); line = tl.createLine()) {
        const float indent = first ? b.indent : 0.f;
        line.setLineWidth(qMax(20.f, contentW - indent));
        const float natural = float(line.ascent() + line.descent());
        const float adv = std::ceil(qMax(natural, b.fontPx * spacing));
        line.setPosition(QPointF(left + indent, y + (adv - natural) / 2.f));
        out.top.push_back(y);
        out.advance.push_back(adv);
        y += adv;
        first = false;
    }
    tl.endLayout();
    out.height = y;
    return out;
}

int ChapterLayout::pageForOffset(int offset) const
{
    if (pages.empty()) return 0;
    auto it = std::upper_bound(pages.begin(), pages.end(), offset, [](int o, const Page& pg) { return o < pg.start; });
    if (it == pages.begin()) return 0;
    return int(std::distance(pages.begin(), it)) - 1;
}

std::shared_ptr<ChapterLayout> layoutChapter(const std::shared_ptr<const ChapterFlow>& flow, const core::Book& book, int spine,
                                             const LayoutParams& p)
{
    auto cl = std::make_shared<ChapterLayout>();
    cl->spine = spine;
    cl->params = p;
    cl->flow = flow;
    const auto& blocks = flow->blocks;
    const size_t n = blocks.size();
    cl->text.resize(n);
    cl->tables.resize(n);
    cl->images.resize(n);

    const float W = p.columnWidth, H = p.pageHeight;
    // A chapter that is a single image (a cover, a plate) fills the page.
    const bool singleImage = n == 1 && blocks[0].kind == Block::Image;

    Page cur;
    float y = 0;
    bool empty = true;
    auto newPage = [&] {
        cl->pages.push_back(std::move(cur));
        cur = Page();
        y = 0;
        empty = true;
    };

    // The smallest height the next block needs on the same page (a heading must not end a page alone).
    auto minHeightOf = [&](size_t i) -> float {
        if (i >= n) return 0;
        const Block& b = blocks[i];
        if (b.kind == Block::Text) {
            if (cl->text[i].advance.empty()) cl->text[i] = layoutText(b, W, p);
            const auto& adv = cl->text[i].advance;
            float h = b.spaceBefore;
            for (size_t k = 0; k < qMin<size_t>(2, adv.size()); ++k) h += adv[k];
            return h;
        }
        if (b.kind == Block::Image) return b.spaceBefore + qMin(H * 0.3f, 100.f);
        return b.spaceBefore + 24.f;
    };

    for (size_t i = 0; i < n; ++i) {
        const Block& b = blocks[i];
        if (b.breakBefore && !empty) newPage();
        const float gap = (empty && !b.chapterStart) ? 0.f : b.spaceBefore;

        if (b.kind == Block::Text) {
            if (cl->text[i].advance.empty()) cl->text[i] = layoutText(b, W, p);
            const TextLines& tl = cl->text[i];
            const int lines = int(tl.advance.size());
            if (b.keepWithNext && !empty && i + 1 < n) {
                const float need = gap + tl.height + minHeightOf(i + 1);
                if (y + need > H + kEpsilon && need <= H) newPage();
            }
            if (b.avoidInside && !empty && y + gap + tl.height > H + kEpsilon && tl.height <= H) newPage();
            y += (empty && !b.chapterStart) ? 0.f : gap;
            int li = 0;
            while (li < lines) {
                int fit = 0;
                float yy = y;
                while (li + fit < lines && yy + tl.advance[size_t(li + fit)] <= H + kEpsilon) {
                    yy += tl.advance[size_t(li + fit)];
                    ++fit;
                }
                if (fit == 0) {
                    if (empty) {
                        fit = 1;   // a line taller than the page: shown alone, clipped
                    } else {
                        newPage();
                        continue;
                    }
                }
                const int remaining = lines - li - fit;
                if (remaining > 0) {
                    // Orphans: don't leave a paragraph's first line alone at the bottom of a page.
                    if (li == 0 && fit < 2 && lines >= 2 && !empty) {
                        newPage();
                        continue;
                    }
                    // Widows: don't carry a paragraph's last line alone to the next page.
                    if (remaining < 2 && lines >= 4) {
                        const int move = 2 - remaining;
                        if (fit - move >= (li == 0 ? 2 : 1)) fit -= move;
                    }
                }
                PageItem item;
                item.kind = PageItem::Text;
                item.block = int(i);
                item.firstLine = li;
                item.lineCount = fit;
                item.dy = y - tl.top[size_t(li)];
                float h = 0;
                for (int k = 0; k < fit; ++k) h += tl.advance[size_t(li + k)];
                item.rect = QRectF(0, y, W, h);
                if (empty) cur.start = textOffsetAtLine(*cl, int(i), li);
                cur.items.push_back(item);
                y += h;
                empty = false;
                li += fit;
                const QTextLine last = tl.layout->lineAt(li - 1);
                cur.end = b.start + last.textStart() + last.textLength();
                if (li < lines) newPage();
            }
        } else if (b.kind == Block::Image) {
            QSize natural;
            const QImage img = loadImage(book, b.imagePath, &natural);
            const QSizeF s = imageSize(b, natural, W, H, p, singleImage);
            if (!empty && y + gap + s.height() > H + kEpsilon) newPage();
            y += empty ? (b.chapterStart && !singleImage ? gap : 0.f) : gap;
            if (singleImage) y = qMax(0.f, float(H - s.height()) / 2.f);
            PageItem item;
            item.kind = PageItem::Image;
            item.block = int(i);
            const float left = blockLeft(b, W), avail = W - left - blockRight(b, W);
            item.rect = QRectF(left + (avail - s.width()) / 2.f, y, s.width(), s.height());
            cl->images[i] = scaledFor(img, s, p.devicePixelRatio);
            if (empty) cur.start = b.start;
            cur.items.push_back(item);
            cur.end = b.start + 1;
            y += float(s.height());
            empty = false;
        } else if (b.kind == Block::Rule) {
            if (!empty && y + gap + 9 > H) newPage();
            y += empty ? 0.f : gap;
            PageItem item;
            item.kind = PageItem::Rule;
            item.block = int(i);
            const float left = blockLeft(b, W);
            // A plain rule is drawn short and centered, as books print it.
            const float avail = W - left - blockRight(b, W);
            const float len = qMin(avail, qMax(avail * 0.3f, 80.f));
            item.rect = QRectF(left + (avail - len) / 2.f, y + 4, len, 1);
            if (empty) cur.start = b.start;
            cur.items.push_back(item);
            cur.end = b.start + 1;
            y += 9;
            empty = false;
        } else if (b.kind == Block::Table) {
            cl->tables[i] = layoutTable(b, book, W, p);
            const TableLayout* t = cl->tables[i].get();
            if (!t) continue;
            y += empty ? 0.f : gap;
            for (size_t r = 0; r < t->rows.size(); ++r) {
                const float h = t->rows[r].height;
                // A header row stays with the row after it.
                const float need = h + (t->headerRow[r] && r + 1 < t->rows.size() ? t->rows[r + 1].height : 0.f);
                if (!empty && (y + h > H + kEpsilon || (y + need > H + kEpsilon && need <= H))) newPage();
                PageItem item;
                item.kind = PageItem::TableRow;
                item.block = int(i);
                item.row = int(r);
                item.rect = QRectF(blockLeft(b, W), y, t->width, h);
                if (empty) cur.start = t->rowStart[r];
                cur.items.push_back(item);
                cur.end = t->rowEnd[r];
                y += h;
                empty = false;
            }
        }
    }
    if (!empty || cl->pages.empty()) {
        if (empty) cur.start = cur.end = 0;
        cl->pages.push_back(std::move(cur));
    }
    // Page starts must increase; an empty page (only possible at the start) begins at 0.
    for (size_t k = 1; k < cl->pages.size(); ++k)
        if (cl->pages[k].start < cl->pages[k - 1].start) cl->pages[k].start = cl->pages[k - 1].end;
    // Drop this thread's text layouts (and with them its font engines); the UI thread rebuilds what it draws.
    {
        QMutexLocker lock(&fontEngineLock());
        for (auto& t : cl->text) t.release();
        for (auto& t : cl->tables)
            if (t) releaseTable(*t);
    }
    return cl;
}

}  // namespace layout

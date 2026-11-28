#include "PdfFormat.h"

#include "core/format/Convert.h"
#include "core/format/pages/PageBook.h"

#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRectF>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#ifdef HAVE_MUPDF
#include <mupdf/fitz.h>
#endif

namespace core {

bool PdfFormat::canOpen(const QString& path, const QByteArray& head) const
{
    if (head.left(1024).contains("%PDF-")) return true;
    const QString s = QFileInfo(path).suffix().toLower();
    return (s == QLatin1String("xps") || s == QLatin1String("oxps")) && head.startsWith("PK");
}

#ifndef HAVE_MUPDF

bool pdf::available() { return false; }

std::shared_ptr<Book> PdfFormat::open(const QString& path, QString* error) const
{
    const bool xps = QFileInfo(path).suffix().toLower().contains(QLatin1String("xps"));
    if (error)
        *error = xps ? QStringLiteral("XPS documents can't be opened on this device yet.")
                     : QStringLiteral("PDF files can't be opened on this device yet.");
    return nullptr;
}

std::shared_ptr<Book> PdfFormat::openDetails(const QString& path, QString* error) const { return open(path, error); }

#else

bool pdf::available() { return true; }

namespace {

// One MuPDF document; every call goes through `mutex` (a MuPDF context serves one thread at a time).
struct Mu {
    fz_context* ctx = nullptr;
    fz_document* doc = nullptr;
    QMutex mutex;
    std::vector<fz_image*> images;   // pictures in the text, turned into files when they are shown

    ~Mu()
    {
        if (!ctx) return;
        for (fz_image* i : images) fz_drop_image(ctx, i);
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }
};

void quiet(void*, const char*) {}

// 0: open; 1: damaged or not a document; 2: locked with a password.
int openDocument(Mu* m, const char* path)
{
    int result = 0;
    fz_var(result);
    fz_try(m->ctx)
    {
        fz_register_document_handlers(m->ctx);
        m->doc = fz_open_document(m->ctx, path);
        if (fz_needs_password(m->ctx, m->doc) && !fz_authenticate_password(m->ctx, m->doc, "")) result = 2;
    }
    fz_catch(m->ctx)
    {
        fz_ignore_error(m->ctx);
        result = 1;
    }
    return result;
}

std::shared_ptr<Mu> openMu(const QString& path, QString* error)
{
    auto m = std::make_shared<Mu>();
    m->ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!m->ctx) {
        if (error) *error = QStringLiteral("There isn't enough memory to open this file.");
        return nullptr;
    }
    fz_set_warning_callback(m->ctx, quiet, nullptr);
    fz_set_error_callback(m->ctx, quiet, nullptr);
    const QByteArray utf8 = path.toUtf8();
    const int r = openDocument(m.get(), utf8.constData());
    const bool xps = QFileInfo(path).suffix().toLower().contains(QLatin1String("xps"));
    if (r == 2) {
        if (error) *error = QStringLiteral("This PDF is locked with a password, so Biblioglot can't open it.");
        return nullptr;
    }
    if (r != 0 || !m->doc) {
        if (error) *error = xps ? QStringLiteral("This XPS document is damaged, or isn't an XPS document.")
                                : QStringLiteral("This PDF is damaged, or isn't a PDF.");
        return nullptr;
    }
    return m;
}

QString metaValue(Mu* m, const char* key)
{
    char buf[2048];
    buf[0] = 0;
    int n = -1;
    fz_var(n);
    fz_try(m->ctx) { n = fz_lookup_metadata(m->ctx, m->doc, key, buf, sizeof buf); }
    fz_catch(m->ctx)
    {
        fz_ignore_error(m->ctx);
        n = -1;
    }
    return n > 0 ? QString::fromUtf8(buf).simplified() : QString();
}

int countPages(Mu* m)
{
    int n = 0;
    fz_var(n);
    fz_try(m->ctx) { n = fz_count_pages(m->ctx, m->doc); }
    fz_catch(m->ctx)
    {
        fz_ignore_error(m->ctx);
        n = 0;
    }
    return n;
}

int pageOfLocation(Mu* m, fz_location loc)
{
    int n = -1;
    fz_var(n);
    fz_try(m->ctx) { n = fz_page_number_from_location(m->ctx, m->doc, loc); }
    fz_catch(m->ctx)
    {
        fz_ignore_error(m->ctx);
        n = -1;
    }
    return n;
}

// The PDF's own contents (bookmarks), flattened in order.
struct OutlineEntry {
    QString title;
    int page = -1;
    float y = 0;
    int depth = 0;
};

// The page (and its height on it) a link leads to; -1 when it leads nowhere in the document.
int resolveLink(Mu* m, const char* uri, float* y)
{
    fz_location* loc = static_cast<fz_location*>(std::malloc(sizeof(fz_location)));
    if (!loc) return -1;
    loc->chapter = -1;
    loc->page = -1;
    fz_try(m->ctx) { *loc = fz_resolve_link(m->ctx, m->doc, uri, nullptr, y); }
    fz_catch(m->ctx) { fz_ignore_error(m->ctx); }
    const fz_location l = *loc;
    std::free(loc);
    return l.page >= 0 ? pageOfLocation(m, l) : -1;
}

void flattenOutline(Mu* m, fz_outline* o, int depth, std::vector<OutlineEntry>& out)
{
    for (; o && out.size() < 5000; o = o->next) {
        OutlineEntry e;
        e.title = QString::fromUtf8(o->title ? o->title : "").simplified();
        e.page = pageOfLocation(m, o->page);
        e.y = o->y;
        e.depth = depth;
        if (e.page < 0 && o->uri) {
            float y = 0;
            e.page = resolveLink(m, o->uri, &y);
            e.y = y;
        }
        if (!e.title.isEmpty() && e.page >= 0) out.push_back(e);
        if (o->down) flattenOutline(m, o->down, depth + 1, out);
    }
}

std::vector<OutlineEntry> loadOutline(Mu* m)
{
    std::vector<OutlineEntry> out;
    fz_outline* o = nullptr;
    fz_var(o);
    fz_try(m->ctx) { o = fz_load_outline(m->ctx, m->doc); }
    fz_catch(m->ctx)
    {
        fz_ignore_error(m->ctx);
        o = nullptr;
    }
    if (o) {
        flattenOutline(m, o, 0, out);
        fz_drop_outline(m->ctx, o);
    }
    return out;
}

// ---- The text of a page ----------------------------------------------------------------------------------------

enum Style { Plain = 0, Bold = 1, Italic = 2 };

struct Run {
    QString text;
    int style = Plain;
};

struct Para {
    std::vector<Run> runs;
    QString plain;
    QRectF box;
    float size = 0;
    int lines = 0;
    bool bold = false;   // all (or nearly all) of it bold
    int image = -1;      // a picture: its index in Mu::images
};

struct LineText {
    std::vector<Run> runs;
    QString plain;
    QRectF box;
    float size = 0;
    bool joined = false;   // MuPDF took away the hyphen at its end: the next line goes on the same word
    int chars = 0, boldChars = 0;
};

// A line of text or a picture, in the page's order; `block` is MuPDF's text block it came from.
struct Element {
    LineText line;
    int image = -1;
    QRectF box;
    int block = 0;
};

struct PageText {
    QRectF box;
    QString label;
    std::vector<Element> elements;   // as read
    std::vector<Para> paras;         // made from them (segment())
    int chars = 0;
    bool scanned = false;   // a picture covers the page: a scan (its text, if any, an invisible layer)
};

QRectF rectOf(const fz_rect& r) { return QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1)); }

bool endsSentence(const QString& s)
{
    for (qsizetype i = s.size() - 1; i >= 0; --i) {
        const QChar c = s[i];
        if (c.isSpace()) continue;
        static const QString enders = QStringLiteral(".!?:;…\"”»’)]");
        return enders.contains(c);
    }
    return false;
}

void addText(std::vector<Run>& runs, const QString& text, int style)
{
    if (text.isEmpty()) return;
    if (!runs.empty() && runs.back().style == style) runs.back().text += text;
    else runs.push_back({text, style});
}

// The lines of a text block, each with its text, type size and styles.
void textBlock(fz_context* ctx, fz_stext_block* b, int block, PageText* out)
{
    for (fz_stext_line* ln = b->u.t.first_line; ln; ln = ln->next) {
        // Text turned on its side (a stamp in the margin, a watermark, a table's rotated head) isn't read.
        // (Vertical Japanese or Chinese text, wmode 1, is read.)
        if (ln->wmode == 0 && (std::abs(ln->dir.y) > 0.2f || ln->dir.x < 0.5f)) continue;
        LineText l;
        std::map<int, int> sizes;   // half points -> characters
        for (fz_stext_char* ch = ln->first_char; ch; ch = ch->next) {
            char32_t c = char32_t(ch->c);
            if (c == U'\t') c = U' ';
            if (c < 0x20 || c == 0xFFFD || (c >= 0xE000 && c <= 0xF8FF)) continue;   // controls, unknown and private-use symbols
            const bool bold = (ch->flags & FZ_STEXT_BOLD) || (ch->font && fz_font_is_bold(ctx, ch->font));
            const bool italic = ch->font && fz_font_is_italic(ctx, ch->font);
            const QString s = QString::fromUcs4(&c, 1);
            addText(l.runs, s, (bold ? Bold : 0) | (italic ? Italic : 0));
            l.plain += s;
            if (c != U' ') {
                ++l.chars;
                if (bold) ++l.boldChars;
                ++sizes[qRound(ch->size * 2)];
            }
        }
        if (l.chars == 0) continue;
        // Dot leaders (a printed contents: "Preámbulo . . . . . . 5"): one ellipsis.
        static const QRegularExpression leader(QStringLiteral("(\\s*\\.){4,}\\s*"));
        if (l.plain.contains(leader)) {
            for (Run& r : l.runs) r.text.replace(leader, QStringLiteral(" … "));
            l.plain.replace(leader, QStringLiteral(" … "));
        }
        int best = 0, bestCount = -1;
        for (const auto& [sz, n] : sizes)
            if (n > bestCount) {
                best = sz;
                bestCount = n;
            }
        l.size = best / 2.f;
        l.box = rectOf(ln->bbox);
        l.joined = ln->flags & FZ_STEXT_LINE_FLAGS_JOINED;
        if (l.joined) {
            // The hyphen may still be there: the word goes on without it.
            const QString t = l.plain.trimmed();
            if (t.endsWith(QLatin1Char('-')) || t.endsWith(QChar(0x00AD)) || t.endsWith(QChar(0x2010))) {
                while (!l.runs.empty()) {
                    QString& r = l.runs.back().text;
                    while (!r.isEmpty() && r.back().isSpace()) r.chop(1);
                    if (!r.isEmpty()) {
                        r.chop(1);
                        break;
                    }
                    l.runs.pop_back();
                }
                l.plain = t.left(t.size() - 1);
            }
        }
        out->chars += int(l.plain.size());
        Element e;
        e.box = l.box;
        e.block = block;
        e.line = std::move(l);
        out->elements.push_back(std::move(e));
    }
}

// The text column of the whole document: the body text's type size, and where most body lines start and end.
struct Column {
    float body = 10;
    qreal left = 0, right = 0;
};

Column measureColumn(const std::vector<PageText>& pages)
{
    Column col;
    std::map<int, qint64> sizeChars;
    for (const auto& pg : pages)
        for (const auto& e : pg.elements)
            if (e.image < 0) sizeChars[qRound(e.line.size * 2)] += e.line.chars;
    qint64 most = -1;
    for (const auto& [sz, c] : sizeChars)
        if (c > most) {
            most = c;
            col.body = sz / 2.f;
        }
    std::vector<qreal> lefts, rights;
    for (const auto& pg : pages)
        for (const auto& e : pg.elements)
            if (e.image < 0 && qAbs(e.line.size - col.body) <= qMax(0.6f, col.body * 0.08f)) {
                lefts.push_back(e.box.left());
                rights.push_back(e.box.right());
            }
    if (!lefts.empty()) {
        std::sort(lefts.begin(), lefts.end());
        std::sort(rights.begin(), rights.end());
        col.left = lefts[lefts.size() / 20];
        col.right = rights[std::min(rights.size() - 1, rights.size() * 19 / 20)];
    }
    return col;
}

// A page's lines into paragraphs. A paragraph ends at a gap wider than the lines' spacing, a change of type size,
// a line that moves up (the next column), or a line that ends short of the column with a sentence's end (prose).
// A line that ends well short of the column without one is a line of verse (or of a list, an address): its line
// break stays, and its stanza stays one paragraph.
std::vector<Para> segment(const PageText& pg, const Column& col)
{
    std::vector<Para> out;
    // A prose block's own right edge (most of its lines end there): a column narrower than the page's.
    QHash<int, qreal> blockRight;
    {
        QHash<int, std::vector<qreal>> rights;
        QHash<int, float> sizes;
        for (const auto& e : pg.elements)
            if (e.image < 0) {
                rights[e.block].push_back(e.box.right());
                sizes[e.block] = qMax(sizes.value(e.block), e.line.size);
            }
        for (auto it = rights.begin(); it != rights.end(); ++it) {
            if (it.value().size() < 3) continue;
            const qreal max = *std::max_element(it.value().begin(), it.value().end());
            const qreal em = qMax(4.f, sizes.value(it.key()));
            const auto near = std::count_if(it.value().begin(), it.value().end(), [&](qreal r) { return r >= max - em; });
            if (near * 2 >= qsizetype(it.value().size())) blockRight.insert(it.key(), max);
        }
    }
    std::vector<qreal> steps;
    {
        const LineText* prev = nullptr;
        for (const auto& e : pg.elements) {
            if (e.image >= 0) {
                prev = nullptr;
                continue;
            }
            if (prev) {
                const qreal d = e.line.box.top() - prev->box.top();
                if (d > 0 && d < 3 * qMax(prev->size, e.line.size)) steps.push_back(d);
            }
            prev = &e.line;
        }
    }
    std::sort(steps.begin(), steps.end());
    const qreal step = steps.empty() ? 1.2 * col.body : steps[steps.size() / 2];

    // A line break that may stay (verse) or go (prose): decided when the paragraph is complete.
    const QChar maybeBreak(0xE000);   // a private-use character: never in the text (those are left out)
    auto startsUpper = [](const QString& s) {
        for (const QChar c : s) {
            if (c.isLetter()) return c.isUpper();
            if (c.isDigit()) return false;
        }
        return false;
    };
    Para p;
    int chars = 0, boldChars = 0;
    double sizeSum = 0;
    int candidates = 0, upper = 0;   // short lines inside the paragraph, and how many are followed by a capital
    auto finish = [&] {
        if (p.lines == 0) return;
        p.size = chars > 0 ? float(sizeSum / chars) : 0;
        p.bold = chars > 0 && boldChars >= chars * 0.8;
        // Verse: most line breaks after short lines, and the next lines start with a capital (in prose that
        // breaks short of the margin, as in a plain-text book, the next line mostly doesn't).
        const bool verse = candidates > 0 && candidates * 2 >= p.lines - 1 && upper * 10 >= candidates * 7;
        const QChar sep = verse ? QLatin1Char('\n') : QLatin1Char(' ');
        for (Run& r : p.runs) r.text.replace(maybeBreak, sep);
        p.plain.replace(maybeBreak, sep);
        out.push_back(std::move(p));
        p = Para();
        chars = boldChars = 0;
        sizeSum = 0;
        candidates = upper = 0;
    };
    const Element* prevEl = nullptr;
    for (const auto& e : pg.elements) {
        if (e.image >= 0) {
            finish();
            Para img;
            img.image = e.image;
            img.box = e.box;
            out.push_back(std::move(img));
            prevEl = nullptr;
            continue;
        }
        const LineText& l = e.line;
        bool maybe = false;
        if (prevEl && p.lines > 0) {
            const LineText& prev = prevEl->line;
            const qreal em = qMax<qreal>(4, prev.size);
            const qreal d = l.box.top() - prev.box.top();
            const qreal right = blockRight.value(prevEl->block, qMax(col.right, prev.box.right()));
            const qreal left = qMin(col.left, prev.box.left());
            const bool looksVerse = candidates > 0 && upper == candidates;   // so far, a poem's lines
            const bool upperNext = startsUpper(l.plain);
            bool brk = false;
            if (d < -em || d > 3.5 * step + em) brk = true;                     // another column, or far below
            else if (d > step * 1.45 && d > em * 1.2) brk = true;               // a gap: a new paragraph or stanza
            else if (qAbs(l.size - prev.size) > qMax<qreal>(1.0, prev.size * 0.12)) brk = true;
            else if (prev.box.right() < right - 2.5 * em && endsSentence(prev.plain)) (looksVerse && upperNext ? maybe : brk) = true;
            else if (l.box.left() > left + 1.0 * em && (prev.box.left() < left + 0.5 * em || p.lines == 1) && endsSentence(prev.plain))
                (looksVerse && upperNext ? maybe : brk) = true;
            else if (!prev.joined && (prev.box.right() < right - 4 * em || looksVerse)) maybe = true;
            if (brk) {
                finish();
                maybe = false;
            }
            if (maybe) {
                ++candidates;
                if (upperNext) ++upper;
            }
        }
        if (p.lines > 0) {
            const int style = p.runs.empty() ? Plain : p.runs.back().style;
            if (maybe) {
                addText(p.runs, QString(maybeBreak), style);
                p.plain += maybeBreak;
            } else if (!prevEl->line.joined && !p.plain.endsWith(QLatin1Char(' ')) && !l.plain.startsWith(QLatin1Char(' '))) {
                addText(p.runs, QStringLiteral(" "), style);
                p.plain += QLatin1Char(' ');
            }
        }
        for (const Run& r : l.runs) addText(p.runs, r.text, r.style);
        p.plain += l.plain;
        p.box = p.lines == 0 ? l.box : p.box.united(l.box);
        ++p.lines;
        chars += l.chars;
        boldChars += l.boldChars;
        sizeSum += double(l.size) * l.chars;
        prevEl = &e;
    }
    finish();
    return out;
}

void walkBlocks(fz_context* ctx, fz_stext_block* b, Mu* m, PageText* out, int* blocks)
{
    const qreal pageArea = qMax<qreal>(1, out->box.width() * out->box.height());
    for (; b; b = b->next) {
        if (b->type == FZ_STEXT_BLOCK_TEXT) {
            textBlock(ctx, b, ++*blocks, out);
        } else if (b->type == FZ_STEXT_BLOCK_IMAGE && b->u.i.image) {
            const QRectF r = rectOf(b->bbox).intersected(out->box);
            const qreal share = r.width() * r.height() / pageArea;
            if (share > 0.7) {
                out->scanned = true;
            } else if (share >= 0.015 && r.width() > 36 && r.height() > 36 && m->images.size() < 20000) {
                m->images.push_back(fz_keep_image(ctx, b->u.i.image));
                Element e;
                e.image = int(m->images.size()) - 1;
                e.box = r;
                e.block = ++*blocks;
                out->elements.push_back(std::move(e));
            }
        } else if (b->type == FZ_STEXT_BLOCK_STRUCT && b->u.s.down) {
            walkBlocks(ctx, b->u.s.down->first_block, m, out, blocks);
        }
    }
}

bool extractPage(Mu* m, int index, PageText* out)
{
    fz_context* ctx = m->ctx;
    fz_page* page = nullptr;
    fz_stext_page* st = nullptr;
    fz_var(page);
    fz_var(st);
    bool ok = true;
    fz_var(ok);
    char label[64];
    label[0] = 0;
    fz_rect* bound = static_cast<fz_rect*>(std::malloc(sizeof(fz_rect)));
    if (!bound) return false;
    *bound = fz_empty_rect;
    fz_try(ctx)
    {
        page = fz_load_page(ctx, m->doc, index);
        *bound = fz_bound_page(ctx, page);
        fz_page_label(ctx, page, label, sizeof label);
        fz_stext_options opts;
        std::memset(&opts, 0, sizeof opts);
        opts.flags = FZ_STEXT_DEHYPHENATE | FZ_STEXT_PRESERVE_IMAGES | FZ_STEXT_MEDIABOX_CLIP;
        st = fz_new_stext_page_from_page(ctx, page, &opts);
    }
    fz_catch(ctx)
    {
        fz_ignore_error(ctx);
        ok = false;
    }
    out->box = rectOf(*bound);
    std::free(bound);
    out->label = QString::fromUtf8(label).simplified();
    int blocks = 0;
    if (ok && st) walkBlocks(ctx, st->first_block, m, out, &blocks);
    fz_drop_stext_page(ctx, st);
    fz_drop_page(ctx, page);
    return ok;
}

// ---- Drawing ---------------------------------------------------------------------------------------------------

QImage renderWith(Mu* m, int index, int longSide)
{
    QMutexLocker lock(&m->mutex);
    fz_context* ctx = m->ctx;
    fz_page* page = nullptr;
    fz_pixmap* pix = nullptr;
    fz_var(page);
    fz_var(pix);
    fz_try(ctx)
    {
        page = fz_load_page(ctx, m->doc, index);
        const fz_rect r = fz_bound_page(ctx, page);
        const float w = r.x1 - r.x0, h = r.y1 - r.y0;
        float zoom = float(longSide) / std::max(1.f, std::max(w, h));
        zoom = std::min(zoom, 8.f);
        pix = fz_new_pixmap_from_page(ctx, page, fz_scale(zoom, zoom), fz_device_rgb(ctx), 0);
    }
    fz_catch(ctx)
    {
        fz_ignore_error(ctx);
    }
    fz_drop_page(ctx, page);
    if (!pix) return {};
    const int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix), stride = fz_pixmap_stride(ctx, pix);
    const int n = fz_pixmap_components(ctx, pix);
    const unsigned char* s = fz_pixmap_samples(ctx, pix);
    QImage img;
    if (n == 3 && w > 0 && h > 0) {
        img = QImage(w, h, QImage::Format_RGB888);
        for (int y = 0; y < h; ++y) std::memcpy(img.scanLine(y), s + size_t(y) * size_t(stride), size_t(w) * 3);
    }
    fz_drop_pixmap(ctx, pix);
    return img;
}

QByteArray imageBytes(Mu* m, int k)
{
    QMutexLocker lock(&m->mutex);
    if (k < 0 || k >= int(m->images.size())) return {};
    fz_context* ctx = m->ctx;
    fz_image* img = m->images[size_t(k)];
    fz_buffer* buf = nullptr;
    fz_var(buf);
    fz_try(ctx)
    {
        fz_compressed_buffer* cb = fz_compressed_image_buffer(ctx, img);
        // A JPEG in plain RGB or grey is passed on as it is; anything else becomes a PNG.
        if (cb && cb->params.type == FZ_IMAGE_JPEG && (img->n == 1 || img->n == 3) && !img->mask && !img->use_decode)
            buf = fz_keep_buffer(ctx, cb->buffer);
        else
            buf = fz_new_buffer_from_image_as_png(ctx, img, fz_default_color_params);
    }
    fz_catch(ctx)
    {
        fz_ignore_error(ctx);
    }
    if (!buf) return {};
    unsigned char* data = nullptr;
    const size_t len = fz_buffer_storage(ctx, buf, &data);
    QByteArray out(reinterpret_cast<const char*>(data), qsizetype(len));
    fz_drop_buffer(ctx, buf);
    return out;
}

class PdfBook : public PageBook {
public:
    std::shared_ptr<Mu> mu;

    QByteArray resource(const QString& path, bool* ok) const override
    {
        if (path.startsWith(QLatin1String("images/pdf"))) {
            bool good = false;
            const int k = path.mid(10, path.lastIndexOf(QLatin1Char('.')) - 10).toInt(&good) - 1;
            const QByteArray data = good ? imageBytes(mu.get(), k) : QByteArray();
            if (ok) *ok = !data.isEmpty();
            return data;
        }
        return PageBook::resource(path, ok);
    }
    bool hasResource(const QString& path) const override
    {
        return path.startsWith(QLatin1String("images/pdf")) || PageBook::hasResource(path);
    }

protected:
    QImage renderPage(int index, int longSide) const override { return renderWith(mu.get(), index, longSide); }
};

QString imagePath(int k) { return QStringLiteral("images/pdf%1.img").arg(k + 1, 5, 10, QLatin1Char('0')); }

// ---- Metadata --------------------------------------------------------------------------------------------------

void readMetadata(Mu* m, PdfBook& book, const QString& path)
{
    QString title = metaValue(m, FZ_META_INFO_TITLE);
    static const QRegularExpression app(QStringLiteral("^(Microsoft (Word|PowerPoint|Excel) - )"), QRegularExpression::CaseInsensitiveOption);
    title.remove(app);
    static const QRegularExpression fileName(QStringLiteral("\\.(docx?|pdf|odt|rtf|txt|indd|qxd|tex|dvi|ps|pub|pages|html?)$"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression junk(QStringLiteral("^(untitled|title|document|sin título|unknown|none|\\W*)$"),
                                         QRegularExpression::CaseInsensitiveOption);
    if (title.contains(fileName) || junk.match(title).hasMatch()) title.clear();
    book.metadata.title = title.isEmpty() ? QFileInfo(path).completeBaseName() : title;
    const QString author = metaValue(m, FZ_META_INFO_AUTHOR);
    for (const QString& a : author.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        if (!a.trimmed().isEmpty() && !junk.match(a.trimmed()).hasMatch()) book.metadata.authors << a.trimmed();
    book.metadata.description = metaValue(m, FZ_META_INFO_SUBJECT);
    book.metadata.coverPath = PageBook::coverPath();
}

QString plainOf(const std::vector<PageText>& pages, int maxChars)
{
    QString s;
    for (const auto& pg : pages) {
        for (const auto& e : pg.elements) {
            s += e.line.plain;
            s += QLatin1Char('\n');
        }
        if (s.size() > maxChars) break;
    }
    return s;
}

// ---- The book's text -------------------------------------------------------------------------------------------

QString runsHtml(const std::vector<Run>& runs)
{
    QString html;
    for (const Run& r : runs) {
        QString e = convert::escape(r.text);
        e.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
        if (r.style & Bold) e = QStringLiteral("<b>") + e + QStringLiteral("</b>");
        if (r.style & Italic) e = QStringLiteral("<i>") + e + QStringLiteral("</i>");
        html += e;
    }
    return html;
}

QString edgeKey(const QString& s)
{
    QString k = s.toLower().simplified();
    static const QRegularExpression digits(QStringLiteral("\\d+"));
    k.replace(digits, QStringLiteral("#"));
    return k;
}

bool isPageNumber(const QString& s)
{
    static const QRegularExpression re(
        QStringLiteral("^[\\s\\-–—|·•.]*((page|página|pág\\.?|p\\.|seite|pagina)\\s*)?([0-9]{1,5}|[ivxlcdm]{1,8})(\\s*(of|de|/|von|di)\\s*[0-9]{1,5})?[\\s\\-–—|·•.]*$"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(s).hasMatch();
}

// The pages' numbers: the PDF's own labels (front matter in Roman numerals, then 1, 2, 3…) when every page has a
// different one; when they start again (an appendix numbered from 1) or are missing, the pages are counted.
QStringList pageLabels(const std::vector<PageText>& pages)
{
    QStringList labels;
    QSet<QString> seen;
    bool usable = true;
    for (size_t i = 0; i < pages.size(); ++i) {
        const QString l = pages[i].label;
        if (l.isEmpty() || seen.contains(l)) usable = false;
        seen.insert(l);
        labels << l;
    }
    if (!usable)
        for (int i = 0; i < labels.size(); ++i) labels[i] = QString::number(i + 1);
    return labels;
}

struct Item {
    enum Kind { Text, Heading, Picture, PagePicture } kind = Text;
    int level = 0;
    QString html;    // the block's inside (a paragraph's text, a heading's text, an <img>)
    QString prefix;  // anchors before the block
    QString plain;
    QStringList ids;
    int page = 0;
    bool chapterStart = false;
    bool firstOfPage = false;
};

void buildText(Mu* m, PdfBook& book, std::vector<PageText>& pages)
{
    const int n = int(pages.size());
    const Column col = measureColumn(pages);
    for (auto& pg : pages) {
        pg.paras = segment(pg, col);
        pg.elements.clear();
    }
    // Line numbers in a margin (poems, plays, law texts): a lone number away from the column's left edge.
    static const QRegularExpression lone(QStringLiteral("^\\s*\\d{1,4}\\s*$"));
    for (auto& pg : pages) {
        std::vector<Para> kept;
        for (auto& p : pg.paras) {
            if (p.image < 0 && p.lines == 1 && !p.bold && p.size <= col.body * 1.15f && lone.match(p.plain).hasMatch() &&
                p.box.left() > col.left + 6 * qMax(4.f, p.size))
                continue;
            kept.push_back(std::move(p));
        }
        pg.paras = std::move(kept);
    }
    // Running heads and page numbers: short lines at a page's top or bottom edge that repeat (numbers aside)
    // on several pages, or that are just a number.
    auto atEdge = [](const PageText& pg, const Para& p) {
        if (p.image >= 0 || p.plain.size() > 120) return false;
        const qreal h = pg.box.height();
        return p.box.bottom() < pg.box.top() + 0.09 * h || p.box.top() > pg.box.bottom() - 0.09 * h;
    };
    QHash<QString, int> edgeCount;
    for (const auto& pg : pages) {
        QSet<QString> seen;
        for (const auto& p : pg.paras)
            if (atEdge(pg, p)) seen.insert(edgeKey(p.plain));
        for (const QString& k : seen) ++edgeCount[k];
    }
    for (auto& pg : pages) {
        std::vector<Para> kept;
        for (auto& p : pg.paras) {
            if (atEdge(pg, p) && (isPageNumber(p.plain) || (n >= 4 && edgeCount.value(edgeKey(p.plain)) >= 3))) continue;
            // A running head that sits lower on a page of its own (the title page): the same line that heads
            // most other pages, in the page's top or bottom quarter.
            if (p.image < 0 && p.plain.size() <= 120 && n >= 4 && edgeCount.value(edgeKey(p.plain)) >= qMax(3, int(n * 0.3))) {
                const qreal h = pg.box.height();
                if (p.box.bottom() < pg.box.top() + 0.25 * h || p.box.top() > pg.box.bottom() - 0.25 * h) continue;
            }
            kept.push_back(std::move(p));
        }
        pg.paras = std::move(kept);
    }

    // The body text's size: the size most of the text is in.
    std::map<int, qint64> sizeChars;
    for (const auto& pg : pages)
        for (const auto& p : pg.paras)
            if (p.image < 0) sizeChars[qRound(p.size * 2)] += p.plain.size();
    float body = 10;
    qint64 most = -1;
    for (const auto& [sz, c] : sizeChars)
        if (c > most) {
            most = c;
            body = sz / 2.f;
        }
    auto headingLevel = [&](const Para& p) {
        if (p.image >= 0) return 0;
        const QString t = p.plain.trimmed();
        if (t.isEmpty() || t.size() > 160 || p.lines > 4) return 0;
        const float ratio = p.size / qMax(1.f, body);
        if (ratio >= 1.6f) return 1;
        if (ratio >= 1.3f) return 2;
        if (ratio >= 1.12f && p.bold) return 3;
        if (p.bold && ratio >= 0.95f && t.size() <= 80 && p.lines <= 2 && !t.endsWith(QLatin1Char('.')) && !t.endsWith(QLatin1Char(',')) &&
            !isPageNumber(t))
            return 3;
        return 0;
    };

    // The outline, as anchors on its pages.
    const std::vector<OutlineEntry> outline = loadOutline(m);
    std::vector<std::vector<std::pair<float, int>>> anchorsOn(static_cast<size_t>(n));
    for (size_t k = 0; k < outline.size(); ++k)
        if (outline[k].page < n) anchorsOn[size_t(outline[k].page)].push_back({outline[k].y, int(k)});
    for (auto& a : anchorsOn) std::stable_sort(a.begin(), a.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    int splitDepth = -1;
    if (outline.size() >= 2) {
        int top = 0, second = 0;
        for (const auto& e : outline) {
            if (e.depth == 0) ++top;
            if (e.depth == 1) ++second;
        }
        splitDepth = top >= 2 ? 0 : second >= 2 ? 1 : -1;
    }

    std::vector<Item> items;
    QString pending;           // anchors waiting for the next block
    QStringList pendingIds;
    bool pendingSplit = false;
    auto anchor = [&](const QString& id, bool split) {
        pending += QStringLiteral("<span id=\"%1\"></span>").arg(id);
        pendingIds << id;
        pendingSplit = pendingSplit || split;
    };
    auto push = [&](Item it) {
        it.prefix = pending;
        it.ids += pendingIds;
        it.chapterStart = it.chapterStart || pendingSplit;
        pending.clear();
        pendingIds.clear();
        pendingSplit = false;
        items.push_back(std::move(it));
    };
    int headingCount = 0;
    for (int i = 0; i < n; ++i) {
        PageText& pg = pages[size_t(i)];
        anchor(QStringLiteral("p%1").arg(i + 1), false);
        auto& anchors = anchorsOn[size_t(i)];
        size_t nextAnchor = 0;
        auto anchorsUpTo = [&](qreal y) {
            for (; nextAnchor < anchors.size() && anchors[nextAnchor].first <= y; ++nextAnchor) {
                const int k = anchors[nextAnchor].second;
                anchor(QStringLiteral("toc%1").arg(k + 1), outline[size_t(k)].depth == splitDepth);
            }
        };
        bool first = true;
        const bool hasText = pg.chars >= 1;
        if (!hasText) {
            anchorsUpTo(1e9);
            Item it;
            it.kind = Item::PagePicture;
            it.html = PageBook::pagePicture(i, pg.label.isEmpty() ? QString::number(i + 1) : pg.label);
            it.page = i;
            it.firstOfPage = true;
            push(std::move(it));
            continue;
        }
        for (Para& p : pg.paras) {
            anchorsUpTo(p.box.top() + qMax<qreal>(2, p.size));
            if (p.image >= 0) {
                Item it;
                it.kind = Item::Picture;
                it.html = QStringLiteral("<img src=\"%1\" alt=\"\"/>").arg(imagePath(p.image));
                it.page = i;
                it.firstOfPage = first;
                first = false;
                push(std::move(it));
                continue;
            }
            const int level = headingLevel(p);
            // A paragraph that goes on from the page before: the same paragraph.
            if (first && level == 0 && pendingIds.size() <= 1 && !pendingSplit && !items.empty() && items.back().kind == Item::Text) {
                Item& prev = items.back();
                const QString t = p.plain.trimmed();
                const bool lower = !t.isEmpty() && t[0].isLower();
                if (lower && !endsSentence(prev.plain)) {
                    QString tail = prev.plain.trimmed();
                    const bool hyphen = tail.size() >= 2 && (tail.endsWith(QLatin1Char('-')) || tail.endsWith(QChar(0x00AD))) && tail[tail.size() - 2].isLetter();
                    if (hyphen) {
                        // "conti-" + "nued": the hyphen goes.
                        const qsizetype at = prev.html.lastIndexOf(tail.back());
                        if (at >= 0) prev.html.remove(at, 1);
                        prev.plain = tail.left(tail.size() - 1);
                    } else {
                        prev.html += QLatin1Char(' ');
                        prev.plain += QLatin1Char(' ');
                    }
                    prev.html += pending + runsHtml(p.runs);
                    prev.plain += p.plain;
                    prev.ids += pendingIds;
                    pending.clear();
                    pendingIds.clear();
                    first = false;
                    continue;
                }
            }
            Item it;
            it.kind = level > 0 ? Item::Heading : Item::Text;
            it.level = level;
            it.html = runsHtml(p.runs);
            it.plain = p.plain.simplified();
            it.page = i;
            it.firstOfPage = first;
            first = false;
            if (level > 0) {
                const QString id = QStringLiteral("h%1").arg(++headingCount);
                it.ids << id;
                it.prefix = QStringLiteral("<span id=\"%1\"></span>").arg(id);   // joined with the anchors in push()
            }
            const QString own = it.prefix;
            const QStringList ownIds = it.ids;
            it.ids.clear();
            push(std::move(it));
            items.back().prefix += own;
            items.back().ids += ownIds;
        }
        anchorsUpTo(1e9);
        // A scanned page whose text is only a few words: the page itself.
        if (pg.scanned && pg.chars < 40) {
            Item it;
            it.kind = Item::PagePicture;
            it.html = PageBook::pagePicture(i, pg.label);
            it.page = i;
            push(std::move(it));
        }
    }
    if (!pending.isEmpty() && !items.empty()) {
        items.back().html += pending;
        items.back().ids += pendingIds;
    }

    // Chapters: at the outline's entries of one level, else at the headings of the highest level used more
    // than once; long stretches also at page starts, so no chapter is very long.
    int splitLevel = 0;
    if (splitDepth < 0) {
        std::map<int, int> perLevel;
        for (const auto& it : items)
            if (it.kind == Item::Heading) ++perLevel[it.level];
        for (const auto& [lvl, c] : perLevel)
            if (c >= 2) {
                splitLevel = lvl;
                break;
            }
        if (splitLevel > 0)
            for (auto& it : items)
                if (it.kind == Item::Heading && it.level == splitLevel) it.chapterStart = true;
    }
    QString lang = book.metadata.language;
    QHash<QString, QString> fileOf;   // id -> chapter file
    QString bodyHtml, title;
    int part = 0;
    qint64 length = 0;
    auto flush = [&] {
        if (bodyHtml.isEmpty()) return;
        const QString file = QStringLiteral("part%1.html").arg(++part, 4, 10, QLatin1Char('0'));
        book.addChapter(file, convert::page(title.isEmpty() ? book.metadata.title : title, bodyHtml, lang));
        for (auto it = fileOf.begin(); it != fileOf.end(); ++it)
            if (it.value().isEmpty()) it.value() = file;
        bodyHtml.clear();
        title.clear();
        length = 0;
    };
    for (const Item& it : items) {
        if (!bodyHtml.isEmpty() && (it.chapterStart || (length > 80000 && it.firstOfPage))) flush();
        if (title.isEmpty() && it.kind == Item::Heading) title = it.plain;
        for (const QString& id : it.ids) fileOf.insert(id, QString());
        switch (it.kind) {
        case Item::Text: bodyHtml += QStringLiteral("<p>") + it.prefix + it.html + QStringLiteral("</p>\n"); break;
        case Item::Heading: {
            const int h = qBound(1, it.level, 4);
            bodyHtml += QStringLiteral("<h%1>").arg(h) + it.prefix + it.html + QStringLiteral("</h%1>\n").arg(h);
            break;
        }
        case Item::Picture:
        case Item::PagePicture:
            bodyHtml += (it.kind == Item::Picture ? QStringLiteral("<div class=\"picture\">") : QStringLiteral("<div>")) + it.prefix + it.html +
                        QStringLiteral("</div>\n");
            break;
        }
        length += it.plain.size() + (it.kind == Item::Text ? 0 : 200);
    }
    flush();

    // Contents: the outline, else the headings.
    if (!outline.empty()) {
        for (size_t k = 0; k < outline.size(); ++k) {
            const QString id = QStringLiteral("toc%1").arg(k + 1);
            const QString file = fileOf.value(id);
            if (!file.isEmpty()) book.addTocEntry(outline[k].title, file + QLatin1Char('#') + id, outline[k].depth);
        }
    } else if (headingCount <= 600) {
        int minLevel = 9;
        for (const auto& it : items)
            if (it.kind == Item::Heading) minLevel = qMin(minLevel, it.level);
        for (const auto& it : items)
            if (it.kind == Item::Heading)
                for (const QString& id : it.ids)
                    if (id.startsWith(QLatin1Char('h')) && !fileOf.value(id).isEmpty()) {
                        book.addTocEntry(it.plain, fileOf.value(id) + QLatin1Char('#') + id, it.level - minLevel);
                        break;
                    }
    }
    // Page numbers: the PDF's own (its page labels), else 1, 2, 3…
    const QStringList labels = pageLabels(pages);
    for (int i = 0; i < n; ++i) {
        const QString id = QStringLiteral("p%1").arg(i + 1);
        const QString file = fileOf.value(id);
        if (!file.isEmpty()) book.pageList.push_back({labels[i], file + QLatin1Char('#') + id});
    }
}

std::shared_ptr<PdfBook> openPdf(const QString& path, QString* error, bool detailsOnly)
{
    std::shared_ptr<Mu> m = openMu(path, error);
    if (!m) return nullptr;
    auto book = std::make_shared<PdfBook>();
    book->filePath = path;
    book->mu = m;
    QMutexLocker lock(&m->mutex);
    readMetadata(m.get(), *book, path);
    book->pageCount = countPages(m.get());
    if (book->pageCount <= 0) {
        if (error) *error = QStringLiteral("This document has no pages.");
        return nullptr;
    }
    std::vector<PageText> pages(size_t(detailsOnly ? qMin(book->pageCount, 12) : book->pageCount));
    int textPages = 0, failed = 0;
    for (size_t i = 0; i < pages.size(); ++i) {
        if (!extractPage(m.get(), int(i), &pages[i])) ++failed;
        if (pages[i].chars >= 20) ++textPages;
    }
    if (failed == int(pages.size())) {
        if (error) *error = QStringLiteral("The pages of this document can't be read: it may be damaged.");
        return nullptr;
    }
    book->metadata.language = convert::guessLanguage(plainOf(pages, 40000));
    if (detailsOnly) return book;

    // A scanned book (pictures of its pages, little or no text): page by page, like a comic.
    if (textPages < qMax(1, int(pages.size() * 0.3))) {
        book->addPicturePages(pageLabels(pages));
        for (const OutlineEntry& e : loadOutline(m.get()))
            if (e.page >= 0 && e.page < book->pageCount)
                book->addTocEntry(e.title, QStringLiteral("page%1.html").arg(e.page + 1, 4, 10, QLatin1Char('0')), e.depth);
        book->metadata.coverPath = PageBook::coverPath();
        return book;
    }
    buildText(m.get(), *book, pages);
    book->metadata.coverPath = PageBook::coverPath();
    return book;
}

}  // namespace

std::shared_ptr<Book> PdfFormat::open(const QString& path, QString* error) const { return openPdf(path, error, false); }

std::shared_ptr<Book> PdfFormat::openDetails(const QString& path, QString* error) const { return openPdf(path, error, true); }

#endif  // HAVE_MUPDF

}  // namespace core

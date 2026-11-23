#include "DjvuFormat.h"

#include "core/format/Convert.h"
#include "core/format/pages/PageBook.h"

#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <functional>

#ifdef HAVE_DJVU
#include <libdjvu/ddjvuapi.h>
#include <libdjvu/miniexp.h>
#endif

namespace core {

bool DjvuFormat::canOpen(const QString& path, const QByteArray& head) const
{
    // "AT&TFORM" then DJVU (one page), DJVM (several) or DJVI.
    if (head.startsWith("AT&TFORM")) return true;
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("djvu") || s == QLatin1String("djv");
}

#ifndef HAVE_DJVU

std::shared_ptr<Book> DjvuFormat::open(const QString&, QString* error) const
{
    if (error) *error = QStringLiteral("DjVu books can't be opened on this device yet.");
    return nullptr;
}

#else

namespace {

struct Djvu {
    ddjvu_context_t* ctx = nullptr;
    ddjvu_document_t* doc = nullptr;
    QMutex mutex;   // DjVuLibre's messages are handled by one thread at a time

    ~Djvu()
    {
        if (doc) ddjvu_document_release(doc);
        if (ctx) ddjvu_context_release(ctx);
    }
    void pump(bool wait)
    {
        if (wait) ddjvu_message_wait(ctx);
        while (ddjvu_message_peek(ctx)) ddjvu_message_pop(ctx);
    }
    // A miniexp result (outline, text, annotations), waiting while DjVuLibre is still decoding it.
    miniexp_t waitFor(const std::function<miniexp_t()>& get)
    {
        miniexp_t r;
        while ((r = get()) == miniexp_dummy) pump(true);
        return r;
    }
};

QString str(miniexp_t e) { return miniexp_stringp(e) ? QString::fromUtf8(miniexp_to_str(e)).simplified() : QString(); }

class DjvuBook : public PageBook {
public:
    std::shared_ptr<Djvu> dj;

protected:
    QImage renderPage(int index, int longSide) const override
    {
        QMutexLocker lock(&dj->mutex);
        ddjvu_page_t* page = ddjvu_page_create_by_pageno(dj->doc, index);
        if (!page) return {};
        while (!ddjvu_page_decoding_done(page)) dj->pump(true);
        if (ddjvu_page_decoding_error(page)) {
            ddjvu_page_release(page);
            return {};
        }
        const int pw = ddjvu_page_get_width(page), ph = ddjvu_page_get_height(page);
        if (pw <= 0 || ph <= 0) {
            ddjvu_page_release(page);
            return {};
        }
        const double scale = std::min(1.0, double(longSide) / std::max(pw, ph));   // never larger than the scan
        const int w = std::max(1, int(pw * scale)), h = std::max(1, int(ph * scale));
        ddjvu_rect_t prect{0, 0, unsigned(w), unsigned(h)};
        ddjvu_rect_t rrect = prect;
        ddjvu_format_t* fmt = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, nullptr);
        ddjvu_format_set_row_order(fmt, 1);
        ddjvu_format_set_y_direction(fmt, 1);
        QImage img(w, h, QImage::Format_RGB888);
        if (!ddjvu_page_render(page, DDJVU_RENDER_COLOR, &prect, &rrect, fmt, size_t(img.bytesPerLine()), reinterpret_cast<char*>(img.bits())))
            img.fill(Qt::white);   // a blank page
        ddjvu_format_release(fmt);
        ddjvu_page_release(page);
        return img;
    }
};

void readOutline(Djvu& dj, DjvuBook& book)
{
    miniexp_t o = dj.waitFor([&] { return ddjvu_document_get_outline(dj.doc); });
    if (!miniexp_consp(o) || miniexp_car(o) != miniexp_symbol("bookmarks")) {
        ddjvu_miniexp_release(dj.doc, o);
        return;
    }
    std::function<void(miniexp_t, int)> walk = [&](miniexp_t list, int depth) {
        for (; miniexp_consp(list); list = miniexp_cdr(list)) {
            const miniexp_t item = miniexp_car(list);
            if (!miniexp_consp(item)) continue;
            const QString title = str(miniexp_car(item));
            const QString url = str(miniexp_cadr(item));
            // "#12" is page 12; "#name" a page's file name, which this book doesn't use.
            bool ok = false;
            const int page = url.startsWith(QLatin1Char('#')) ? url.mid(1).toInt(&ok) : 0;
            if (ok && page >= 1 && page <= book.pageCount && !title.isEmpty())
                book.addTocEntry(title, QStringLiteral("page%1.html").arg(page, 4, 10, QLatin1Char('0')), depth);
            walk(miniexp_cddr(item), depth + 1);
        }
    };
    walk(miniexp_cdr(o), 0);
    ddjvu_miniexp_release(dj.doc, o);
}

void readMetadata(Djvu& dj, DjvuBook& book)
{
    miniexp_t a = dj.waitFor([&] { return ddjvu_document_get_anno(dj.doc, 1); });
    for (miniexp_t l = a; miniexp_consp(l); l = miniexp_cdr(l)) {
        const miniexp_t item = miniexp_car(l);
        if (!miniexp_consp(item) || miniexp_car(item) != miniexp_symbol("metadata")) continue;
        for (miniexp_t kv = miniexp_cdr(item); miniexp_consp(kv); kv = miniexp_cdr(kv)) {
            const miniexp_t pair = miniexp_car(kv);
            if (!miniexp_consp(pair) || !miniexp_symbolp(miniexp_car(pair))) continue;
            const QString key = QString::fromUtf8(miniexp_to_name(miniexp_car(pair))).toLower();
            const QString value = str(miniexp_cadr(pair));
            if (value.isEmpty()) continue;
            if (key == QLatin1String("title")) book.metadata.title = value;
            else if (key == QLatin1String("author")) book.metadata.authors << value;
            else if (key == QLatin1String("publisher")) book.metadata.publisher = value;
        }
    }
    ddjvu_miniexp_release(dj.doc, a);
}

QString pageText(Djvu& dj, int index)
{
    miniexp_t t = dj.waitFor([&] { return ddjvu_document_get_pagetext(dj.doc, index, "page"); });
    QString out;
    std::function<void(miniexp_t)> walk = [&](miniexp_t e) {
        for (; miniexp_consp(e); e = miniexp_cdr(e)) {
            const miniexp_t c = miniexp_car(e);
            if (miniexp_stringp(c)) out += QString::fromUtf8(miniexp_to_str(c)) + QLatin1Char(' ');
            else if (miniexp_consp(c)) walk(c);
        }
    };
    walk(t);
    ddjvu_miniexp_release(dj.doc, t);
    return out;
}

}  // namespace

std::shared_ptr<Book> DjvuFormat::open(const QString& path, QString* error) const
{
    auto dj = std::make_shared<Djvu>();
    dj->ctx = ddjvu_context_create("FreeReader");
    if (!dj->ctx) {
        if (error) *error = QStringLiteral("There isn't enough memory to open this file.");
        return nullptr;
    }
    const QByteArray utf8 = path.toUtf8();
    dj->doc = ddjvu_document_create_by_filename_utf8(dj->ctx, utf8.constData(), 1);
    if (!dj->doc) {
        if (error) *error = QStringLiteral("This DjVu book is damaged, or isn't a DjVu book.");
        return nullptr;
    }
    while (!ddjvu_document_decoding_done(dj->doc)) dj->pump(true);
    if (ddjvu_document_decoding_error(dj->doc)) {
        if (error) *error = QStringLiteral("This DjVu book is damaged, or isn't a DjVu book.");
        return nullptr;
    }
    auto book = std::make_shared<DjvuBook>();
    book->filePath = path;
    book->dj = dj;
    book->pageCount = ddjvu_document_get_pagenum(dj->doc);
    if (book->pageCount <= 0) {
        if (error) *error = QStringLiteral("This DjVu book has no pages.");
        return nullptr;
    }
    QMutexLocker lock(&dj->mutex);
    readMetadata(*dj, *book);
    if (book->metadata.title.isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();
    QString sample;
    for (int i = 0; i < book->pageCount && i < 30 && sample.size() < 20000; ++i) sample += pageText(*dj, i);
    book->metadata.language = convert::guessLanguage(sample);
    book->addPicturePages({});
    readOutline(*dj, *book);
    return book;
}

#endif  // HAVE_DJVU

}  // namespace core

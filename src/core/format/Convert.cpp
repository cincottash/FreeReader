#include "Convert.h"

#include "core/css/Css.h"

#include <QBuffer>
#include <QImageReader>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#ifdef HAVE_WEBP
#include <webp/decode.h>
#endif

namespace core {

namespace {

// A path inside diskRoot, or empty (no "..", no absolute paths: only the book's own folder is read).
QString insideRoot(const QString& root, const QString& path)
{
    if (root.isEmpty() || path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char(':'))) return {};
    const QString clean = QDir::cleanPath(path);
    if (clean.startsWith(QLatin1String(".."))) return {};
    return QDir(root).filePath(clean);
}

int be16le(const QByteArray& d, int at)
{
    if (at + 2 > d.size()) return 0;
    return uchar(d[at]) | (uchar(d[at + 1]) << 8);
}

}  // namespace

QByteArray MemoryBook::resource(const QString& path, bool* ok) const
{
    const auto it = files.constFind(path);
    if (it != files.cend()) {
        if (ok) *ok = true;
        return *it;
    }
    if (zipOpen && zip.contains(path)) return zip.read(path, ok);
    const QString file = insideRoot(diskRoot, path);
    if (!file.isEmpty()) {
        QFile f(file);
        if (f.open(QIODevice::ReadOnly)) {
            if (ok) *ok = true;
            return f.readAll();
        }
    }
    if (ok) *ok = false;
    return {};
}

bool MemoryBook::hasResource(const QString& path) const
{
    if (files.contains(path) || (zipOpen && zip.contains(path))) return true;
    const QString file = insideRoot(diskRoot, path);
    return !file.isEmpty() && QFileInfo(file).isFile();
}

void MemoryBook::addChapter(const QString& path, const QByteArray& html, const QString& title, int depth)
{
    files.insert(path, html);
    SpineItem s;
    s.path = path;
    s.id = QStringLiteral("c%1").arg(spine.size() + 1);
    spine.push_back(s);
    if (!title.trimmed().isEmpty()) addTocEntry(title, path, depth);
}

void MemoryBook::addTocEntry(const QString& title, const QString& href, int depth)
{
    TocEntry e;
    e.title = title.simplified();
    e.href = href;
    e.depth = qMax(0, depth);
    // The parent: the nearest earlier entry one level up.
    for (int i = int(toc.size()) - 1; i >= 0; --i)
        if (toc[size_t(i)].depth < e.depth) {
            e.parent = i;
            e.depth = toc[size_t(i)].depth + 1;
            break;
        }
    if (e.parent < 0) e.depth = 0;
    toc.push_back(e);
}

namespace convert {

QString escape(const QString& text)
{
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"': out += QLatin1String("&quot;"); break;
        default: out += c;
        }
    }
    return out;
}

QByteArray page(const QString& title, const QString& body, const QString& lang, const QString& css)
{
    QString s = QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\"");
    if (!lang.isEmpty()) s += QStringLiteral(" lang=\"%1\" xml:lang=\"%1\"").arg(escape(lang));
    s += QStringLiteral("><head><meta charset=\"utf-8\"/><title>") + escape(title) + QStringLiteral("</title>");
    if (!css.isEmpty()) s += QStringLiteral("<style>") + css + QStringLiteral("</style>");
    s += QStringLiteral("</head><body>\n") + body + QStringLiteral("\n</body></html>\n");
    return s.toUtf8();
}

QString decodeAs(const QByteArray& data, const QString& encoding)
{
    const QString e = encoding.trimmed().toLower();
    if (e.isEmpty()) return decodeText(data);
    if (e == QLatin1String("windows-1252") || e == QLatin1String("cp1252") || e == QLatin1String("1252")) {
        // Qt's decoder may not know Windows-1252 everywhere (Android): the 32 characters that differ from Latin-1.
        static const char16_t high[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                          0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                          0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
        QString out;
        out.resize(data.size());
        for (qsizetype i = 0; i < data.size(); ++i) {
            const uchar b = uchar(data[i]);
            out[i] = (b >= 0x80 && b < 0xA0) ? QChar(high[b - 0x80]) : QChar(b);
        }
        return out;
    }
    if (e == QLatin1String("windows-1251") || e == QLatin1String("cp1251") || e == QLatin1String("1251")) {
        // Cyrillic (many FB2 books): the table for 0x80-0xBF, then А-я in order.
        static const char16_t high[64] = {
            0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
            0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
            0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
            0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457};
        QString out;
        out.resize(data.size());
        for (qsizetype i = 0; i < data.size(); ++i) {
            const uchar b = uchar(data[i]);
            out[i] = b < 0x80 ? QChar(b) : b < 0xC0 ? QChar(high[b - 0x80]) : QChar(char16_t(0x0410 + (b - 0xC0)));
        }
        return out;
    }
    if (e == QLatin1String("iso-8859-1") || e == QLatin1String("latin1") || e == QLatin1String("latin-1") || e == QLatin1String("us-ascii")
        || e == QLatin1String("ascii"))
        return decodeAs(data, QStringLiteral("windows-1252"));
    if (e == QLatin1String("utf-8") || e == QLatin1String("utf8") || e == QLatin1String("65001")) {
        QString s = QString::fromUtf8(data);
        if (s.startsWith(QChar(0xFEFF))) s.remove(0, 1);
        return s;
    }
    auto enc = QStringConverter::encodingForName(e.toLatin1().constData());
    if (enc) {
        QStringDecoder d(*enc);
        QString s = d(data);
        if (!d.hasError()) {
            if (s.startsWith(QChar(0xFEFF))) s.remove(0, 1);
            return s;
        }
    }
    return decodeText(data);
}

QString decodeText(const QByteArray& data)
{
    if (data.startsWith("\xEF\xBB\xBF")) return QString::fromUtf8(data.mid(3));
    if (data.startsWith("\xFF\xFE")) return QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData() + 2), (data.size() - 2) / 2);
    if (data.startsWith("\xFE\xFF")) {
        QStringDecoder d(QStringConverter::Utf16BE);
        return d(data.mid(2));
    }
    // UTF-16 without a BOM: every other byte zero in the first bytes of ordinary text.
    if (data.size() >= 4 && data[1] == 0 && data[3] == 0 && data[0] != 0) return QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData()), data.size() / 2);
    QStringDecoder utf8(QStringConverter::Utf8);
    const QString s = utf8(data);
    if (!utf8.hasError()) return s;
    return decodeAs(data, QStringLiteral("windows-1252"));
}

QString guessLanguage(const QString& text)
{
    struct Lang {
        const char* code;
        QSet<QString> words;
    };
    auto set = [](const char* list) {
        QSet<QString> s;
        for (const QString& w : QString::fromUtf8(list).split(QLatin1Char(' '), Qt::SkipEmptyParts)) s.insert(w);
        return s;
    };
    // Common words that tell the languages apart: each word in one list only. Words common in two of them ("un",
    // "una", "con", "era", "del", "il", "para", "es") are left out: before 2026-11-24 the Italian list had several
    // Spanish ones, and "En un lugar de la Mancha…" came out Italian.
    static const std::vector<Lang> langs = {
        {"es", set("el los las y por su sus pero más muy sin cuando también ya ni hasta donde había fue señor ella ellos "
                   "después aunque entonces él esto eso estaba tiene")},
        {"en", set("the of and to is that it was he for with as his on be at by you had not but from they this have which she her were there")},
        {"fr", set("le les et des du une est pour qui dans pas ne au elle je vous avec sur ce cette sont mais ou plus être tout fait")},
        {"pt", set("o os do da dos das em um uma não com é ao à seu sua ele ela foi muito também quando isso pelo pela")},
        {"it", set("di che è per non della sono ma gli nel alla anche come più questo perché questa molto essere stato")},
        {"de", set("der die und den von zu das mit sich des auf für ist im dem nicht ein eine als auch er dass sie nach wie ich")},
    };
    QHash<QString, int> score;
    int n = 0;
    static const QRegularExpression wordRe(QStringLiteral("[\\p{L}]+"));
    auto it = wordRe.globalMatch(text.left(60000));
    while (it.hasNext() && n < 6000) {
        const QString w = it.next().captured().toLower();
        ++n;
        for (const Lang& l : langs)
            if (l.words.contains(w)) ++score[QLatin1String(l.code)];
    }
    QString best;
    int top = 0, second = 0;
    for (auto s = score.cbegin(); s != score.cend(); ++s) {
        if (s.value() > top) {
            second = top;
            top = s.value();
            best = s.key();
        } else if (s.value() > second) {
            second = s.value();
        }
    }
    if (top < 12 || top < second * 13 / 10) return {};
    return best;
}

QString imageType(const QByteArray& d)
{
    if (d.startsWith("\xFF\xD8\xFF")) return QStringLiteral("image/jpeg");
    if (d.startsWith("\x89PNG\r\n\x1A\n")) return QStringLiteral("image/png");
    if (d.startsWith("GIF87a") || d.startsWith("GIF89a")) return QStringLiteral("image/gif");
    if (d.size() > 12 && d.startsWith("RIFF") && d.mid(8, 4) == "WEBP") return QStringLiteral("image/webp");
    if (d.startsWith("BM") && d.size() > 26) return QStringLiteral("image/bmp");
    if (d.startsWith("II*\0") || d.startsWith("MM\0*")) return QStringLiteral("image/tiff");
    const QByteArray head = d.left(400).trimmed();
    if (head.startsWith("<svg") || ((head.startsWith("<?xml") || head.startsWith("<!--")) && head.contains("<svg"))) return QStringLiteral("image/svg+xml");
    return {};
}

QString imageSuffix(const QString& mimeType)
{
    if (mimeType == QLatin1String("image/jpeg")) return QStringLiteral("jpg");
    if (mimeType == QLatin1String("image/svg+xml")) return QStringLiteral("svg");
    if (mimeType.startsWith(QLatin1String("image/"))) return mimeType.mid(6);
    return QStringLiteral("bin");
}

QString bookSuffixFromBytes(const QByteArray& head)
{
    if (head.startsWith("PK\x03\x04")) {
        if (head.contains("application/epub+zip")) return QStringLiteral("epub");
        if (head.contains("application/vnd.oasis.opendocument.text")) return QStringLiteral("odt");
        // XPS starts with [Content_Types].xml like Word, then its fixed document sequence.
        if (head.contains(".fdseq") || head.contains("FixedDocumentSequence") || head.contains("Documents/1/")) return QStringLiteral("xps");
        if (head.contains("[Content_Types].xml") || head.contains("word/")) return QStringLiteral("docx");
        if (head.contains(".fb2")) return QStringLiteral("fb2.zip");
        // The first entry's name: pictures make a comic.
        const QByteArray name = head.mid(30, qMin<int>(be16le(head, 26), 200)).toLower();
        for (const char* ext : {".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp"})
            if (name.endsWith(ext)) return QStringLiteral("cbz");
        if (name.endsWith("comicinfo.xml")) return QStringLiteral("cbz");
        return QStringLiteral("zip");
    }
    if (head.mid(60, 8) == "BOOKMOBI") return QStringLiteral("mobi");
    if (head.mid(60, 8) == "TEXtREAd") return QStringLiteral("pdb");
    if (head.startsWith("%PDF")) return QStringLiteral("pdf");
    if (head.startsWith("AT&TFORM")) return QStringLiteral("djvu");
    if (head.startsWith("Rar!\x1A\x07")) return QStringLiteral("cbr");
    if (head.startsWith("7z\xBC\xAF\x27\x1C")) return QStringLiteral("cb7");
    if (head.startsWith("{\\rtf")) return QStringLiteral("rtf");
    if (head.startsWith("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1")) return QStringLiteral("doc");
    if (head.startsWith("ITSF")) return QStringLiteral("chm");
    if (head.size() >= 262 && head.mid(257, 5) == "ustar") return QStringLiteral("cbt");
    const QByteArray lower = head.left(2048).toLower();
    if (lower.contains("<fictionbook")) return QStringLiteral("fb2");
    if (lower.contains("<html") || lower.contains("<!doctype html")) return QStringLiteral("html");
    if (!head.contains('\0') && !head.isEmpty()) return QStringLiteral("txt");
    return {};
}

bool naturalLess(const QString& a, const QString& b)
{
    // Runs of digits compare as numbers, the rest without case (QCollator's numeric mode isn't on every platform).
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            qsizetype i2 = i, j2 = j;
            while (i2 < a.size() && a[i2].isDigit()) ++i2;
            while (j2 < b.size() && b[j2].isDigit()) ++j2;
            QStringView da = QStringView(a).mid(i, i2 - i), db = QStringView(b).mid(j, j2 - j);
            while (da.size() > 1 && da[0] == u'0') da = da.mid(1);
            while (db.size() > 1 && db[0] == u'0') db = db.mid(1);
            if (da.size() != db.size()) return da.size() < db.size();
            const int c = da.compare(db);
            if (c != 0) return c < 0;
            i = i2;
            j = j2;
            continue;
        }
        const QChar ca = a[i].toCaseFolded(), cb = b[j].toCaseFolded();
        if (ca != cb) return ca < cb;
        ++i;
        ++j;
    }
    return (a.size() - i) < (b.size() - j);
}

QByteArray imagePage(const QString& title, const QString& imageHref, const QString& lang)
{
    return page(title, QStringLiteral("<div><img data-page-image=\"1\" src=\"%1\" alt=\"%2\"/></div>").arg(escape(imageHref), escape(title)), lang);
}

QImage decodeImage(const QByteArray& data, QSize* natural, int maxHeight)
{
    QBuffer buf;
    buf.setData(data);
    buf.open(QIODevice::ReadOnly);
    QImageReader r(&buf);
    r.setAutoTransform(true);
    const QSize s = r.size();
    if (natural) *natural = s;
    if (maxHeight > 0 && s.isValid() && s.height() > maxHeight) r.setScaledSize(s.scaled(QSize(maxHeight * 4, maxHeight), Qt::KeepAspectRatio));
    QImage img = r.read();
#ifdef HAVE_WEBP
    if (img.isNull() && imageType(data) == QLatin1String("image/webp")) {
        int w = 0, h = 0;
        if (WebPGetInfo(reinterpret_cast<const uint8_t*>(data.constData()), size_t(data.size()), &w, &h) && w > 0 && h > 0) {
            QImage out(w, h, QImage::Format_RGBA8888);
            if (WebPDecodeRGBAInto(reinterpret_cast<const uint8_t*>(data.constData()), size_t(data.size()), out.bits(), size_t(out.sizeInBytes()),
                                   int(out.bytesPerLine()))) {
                if (natural) *natural = QSize(w, h);
                img = maxHeight > 0 && h > maxHeight ? out.scaledToHeight(maxHeight, Qt::SmoothTransformation) : out;
            }
        }
    }
#endif
    return img;
}

QByteArray coverPng(const QImage& image, int maxSide)
{
    if (image.isNull()) return {};
    QImage img = image;
    if (qMax(img.width(), img.height()) > maxSide) img = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

void Chapters::heading(int level, const QString& html, const QString& plain, const QString& id)
{
    m_items.push_back({qBound(1, level, 6), html, plain.simplified(), id, int(plain.size())});
}

void Chapters::block(const QString& html, int textLength)
{
    m_items.push_back({0, html, {}, {}, textLength >= 0 ? textLength : int(html.size())});
}

void Chapters::pageBreak()
{
    if (!m_items.empty() && m_items.back().level != -1) m_items.push_back({-1, {}, {}, {}, 0});
}

void Chapters::finish(MemoryBook& book, const QString& dir) const
{
    int count[7] = {};
    for (const Item& it : m_items)
        if (it.level > 0) ++count[it.level];
    int split = 0;
    for (int l = 1; l <= 6 && !split; ++l)
        if (count[l] >= 2) split = l;
    if (!split)
        for (int l = 1; l <= 6 && !split; ++l)
            if (count[l] == 1) split = l;
    const bool chunk = split == 0 || [&] {
        // Headings far apart are fine; one heading in a long book isn't enough to split it.
        int total = 0;
        for (const Item& it : m_items) total += it.length;
        return count[split] == 1 && total > 120000;
    }();
    int minLevel = split ? split : 1;
    struct Part {
        QString html, title;
        int depth = 0;
        QStringList ids;
    };
    std::vector<Part> parts(1);
    int size = 0;
    std::vector<std::pair<int, QPair<QString, QString>>> subEntries;   // part, (title, id) of headings one level down
    for (const Item& it : m_items) {
        const bool startsPart = (it.level > 0 && it.level <= split && !chunk) || (chunk && size > 60000 && (it.level != 0 || it.level == -1))
                                || (chunk && size > 90000);
        if (startsPart && (!parts.back().html.trimmed().isEmpty())) {
            parts.push_back({});
            size = 0;
        }
        if (it.level == -1) continue;
        if (it.level > 0 && parts.back().title.isEmpty() && (it.level <= split || chunk)) {
            parts.back().title = it.plain;
            parts.back().depth = qMax(0, it.level - minLevel);
        }
        QString html = it.html;
        if (it.level > 0) {
            const QString id = it.id.isEmpty() ? QStringLiteral("h%1").arg(&it - m_items.data()) : it.id;
            html = QStringLiteral("<h%1 id=\"%2\">%3</h%1>\n").arg(it.level).arg(escape(id), it.html);
            if (!chunk && it.level == split + 1) subEntries.push_back({int(parts.size()) - 1, {it.plain, id}});
            parts.back().ids << id;
        }
        parts.back().html += html;
        size += it.length;
    }
    const QString lang = book.metadata.language;
    auto file = [&](size_t i) { return dir + QStringLiteral("part%1.html").arg(int(i + 1), 4, 10, QLatin1Char('0')); };
    // Which part holds each id (headings, and ids written into blocks).
    QHash<QString, size_t> idPart;
    static const QRegularExpression idRe(QStringLiteral("\\bid=\"([^\"]+)\""));
    for (size_t i = 0; i < parts.size(); ++i) {
        auto m = idRe.globalMatch(parts[i].html);
        while (m.hasNext()) idPart.insert(m.next().captured(1), i);
    }
    static const QRegularExpression hrefRe(QStringLiteral("href=\"#([^\"]+)\""));
    for (size_t i = 0; i < parts.size(); ++i) {
        QString html = parts[i].html;
        QString out;
        qsizetype last = 0;
        auto m = hrefRe.globalMatch(html);
        while (m.hasNext()) {
            const auto x = m.next();
            const auto at = idPart.constFind(x.captured(1));
            out += html.mid(last, x.capturedStart() - last);
            out += at == idPart.cend() || *at == i ? x.captured() : QStringLiteral("href=\"%1#%2\"").arg(QFileInfo(file(*at)).fileName(), x.captured(1));
            last = x.capturedEnd();
        }
        html = out + html.mid(last);
        const QString title = parts[i].title;
        book.addChapter(file(i), page(title.isEmpty() ? book.metadata.title : title, html, lang), parts.size() > 1 || !title.isEmpty() ? title : QString(), parts[i].depth);
        for (const auto& [p, entry] : subEntries)
            if (p == int(i)) book.addTocEntry(entry.first, file(i) + QLatin1Char('#') + entry.second, parts[i].depth + 1);
    }
}

}  // namespace convert
}  // namespace core

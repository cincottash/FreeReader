#include "Convert.h"

#include "core/css/Css.h"

#include <QBuffer>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

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
    // Common words that tell the languages apart (words shared by several are left out).
    static const std::vector<Lang> langs = {
        {"es", set("el los las del y por con una su sus es para como pero más muy sin sobre cuando también porque está ya ni hasta donde había fue era señor ella ellos")},
        {"en", set("the of and to is that it was he for with as his on be at by you had not but from they this have which she her were there")},
        {"fr", set("le les et des du un une est pour qui dans pas ne au il elle je vous avec sur ce cette sont mais ou plus être tout fait")},
        {"pt", set("o os do da dos das em um uma não com para é ao à seu sua mas ele ela foi era muito também quando isso pelo pela")},
        {"it", set("il di e che è per un una non del della si sono ma lo gli con nel alla anche come più questo ha era")},
        {"de", set("der die und den von zu das mit sich des auf für ist im dem nicht ein eine als auch es an er so dass sie nach wie ich")},
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
    return page(title, QStringLiteral("<div><img src=\"%1\" alt=\"%2\"/></div>").arg(escape(imageHref), escape(title)), lang);
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

}  // namespace convert
}  // namespace core

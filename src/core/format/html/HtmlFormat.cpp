#include "HtmlFormat.h"

#include "core/css/Css.h"
#include "core/format/Convert.h"
#include "core/format/comic/ComicFormat.h"
#include "core/format/fb2/Fb2Format.h"
#include "core/format/text/TextFormats.h"
#include "core/html/HtmlParser.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <functional>

namespace core {

namespace {

bool hasSuffix(const QString& path, std::initializer_list<const char*> suffixes)
{
    const QString s = QFileInfo(path).suffix().toLower();
    for (const char* x : suffixes)
        if (s == QLatin1String(x)) return true;
    return false;
}

const QSet<QString>& voidElements()
{
    static const QSet<QString> s = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"};
    return s;
}

const QSet<QString>& closesParagraph()
{
    static const QSet<QString> s = {"p",  "div", "h1",  "h2",    "h3",         "h4",   "h5",      "h6",     "table",  "ul",
                                    "ol", "dl",  "pre", "blockquote", "section", "article", "header", "footer", "hr", "figure"};
    return s;
}

struct Tag {
    qsizetype start = -1, end = -1;   // '<' and one past '>'
    QString name;
    bool closing = false, selfClosing = false, special = false;   // special: a comment, <!DOCTYPE>, <?xml?>
};

// The tag starting at or after `from`.
Tag nextTag(const QString& s, qsizetype from)
{
    Tag t;
    const qsizetype lt = s.indexOf(QLatin1Char('<'), from);
    if (lt < 0) return t;
    t.start = lt;
    if (s.mid(lt, 4) == QLatin1String("<!--")) {
        const qsizetype e = s.indexOf(QLatin1String("-->"), lt + 4);
        t.end = e < 0 ? s.size() : e + 3;
        t.special = true;
        return t;
    }
    if (lt + 1 < s.size() && (s[lt + 1] == QLatin1Char('!') || s[lt + 1] == QLatin1Char('?'))) {
        const qsizetype e = s.indexOf(QLatin1Char('>'), lt);
        t.end = e < 0 ? s.size() : e + 1;
        t.special = true;
        return t;
    }
    qsizetype p = lt + 1;
    if (p < s.size() && s[p] == QLatin1Char('/')) {
        t.closing = true;
        ++p;
    }
    qsizetype q = p;
    while (q < s.size() && (s[q].isLetterOrNumber() || s[q] == QLatin1Char(':') || s[q] == QLatin1Char('-'))) ++q;
    t.name = s.mid(p, q - p).toLower();
    if (const qsizetype c = t.name.indexOf(QLatin1Char(':')); c >= 0) t.name = t.name.mid(c + 1);
    QChar quote;
    qsizetype k = q;
    for (; k < s.size(); ++k) {
        const QChar c = s[k];
        if (!quote.isNull()) {
            if (c == quote) quote = QChar();
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
        } else if (c == QLatin1Char('>')) {
            break;
        }
    }
    t.end = k < s.size() ? k + 1 : s.size();
    t.selfClosing = k > 0 && k < s.size() && s[k - 1] == QLatin1Char('/');
    if (t.name.isEmpty()) t.special = true;
    return t;
}

// Whether a heading (h1-h3) starts soon after `from`, before any text.
bool headingFollows(const QString& s, qsizetype from)
{
    qsizetype i = from;
    const qsizetype limit = qMin(s.size(), from + 600);
    while (i < limit) {
        const Tag t = nextTag(s, i);
        if (t.start < 0 || t.start >= limit) return false;
        if (!s.mid(i, t.start - i).trimmed().isEmpty()) return false;
        if (!t.special && !t.closing && (t.name == QLatin1String("h1") || t.name == QLatin1String("h2") || t.name == QLatin1String("h3"))) return true;
        if (!t.special && !t.closing && (t.name == QLatin1String("p") || t.name == QLatin1String("div") || t.name == QLatin1String("a")
                                          || t.name == QLatin1String("span") || t.name == QLatin1String("hr") || t.name == QLatin1String("br")))
            ;   // wrappers and anchors before the heading are fine
        else if (!t.special && !t.closing && t.name != QLatin1String("img"))
            return false;
        i = t.end;
    }
    return false;
}

// Where chapters start in a page's body: its top-level headings, and top-level blocks that begin with one.
std::vector<qsizetype> chapterStarts(const QString& body)
{
    std::vector<qsizetype> out;
    QStringList stack;
    qsizetype i = 0;
    while (i < body.size()) {
        const Tag t = nextTag(body, i);
        if (t.start < 0) break;
        i = t.end;
        if (t.special) continue;
        if (t.closing) {
            const int at = int(stack.lastIndexOf(t.name));
            if (at >= 0) stack = stack.mid(0, at);
            continue;
        }
        // HTML's implied end tags: a new paragraph or block closes an open <p>; a new <li> closes the last one.
        if (!stack.isEmpty() && stack.last() == QLatin1String("p") && closesParagraph().contains(t.name)) stack.removeLast();
        if (!stack.isEmpty() && (t.name == QLatin1String("li") || t.name == QLatin1String("dt") || t.name == QLatin1String("dd"))
            && stack.last() == t.name)
            stack.removeLast();
        if (stack.isEmpty()) {
            const bool heading = t.name == QLatin1String("h1") || t.name == QLatin1String("h2");
            const bool wrapper = (t.name == QLatin1String("div") || t.name == QLatin1String("section") || t.name == QLatin1String("article"))
                                 && headingFollows(body, t.end);
            if ((heading || wrapper) && t.start > 0) out.push_back(t.start);
        }
        if (t.name == QLatin1String("script") || t.name == QLatin1String("style")) {
            const qsizetype e = body.indexOf(QStringLiteral("</") + t.name, t.end, Qt::CaseInsensitive);
            i = e < 0 ? body.size() : e;
            continue;
        }
        if (!t.selfClosing && !voidElements().contains(t.name)) stack << t.name;
    }
    return out;
}

QString plainText(const QString& htmlFragment)
{
    const auto doc = dom::parseText(QStringLiteral("<div>") + htmlFragment + QStringLiteral("</div>"));
    return doc->root->textContent().simplified();
}

// The first h1-h3 of a part: its text and level.
QPair<QString, int> firstHeading(const QString& part)
{
    static const QRegularExpression re(QStringLiteral("<h([1-3])\\b[^>]*>(.*?)</h\\1\\s*>"),
                                       QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(part);
    if (!m.hasMatch()) return {};
    return {plainText(m.captured(2)).left(120), m.captured(1).toInt()};
}

QString metaContent(const QString& head, const QStringList& names)
{
    static const QRegularExpression meta(QStringLiteral("<meta\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression nameRe(QStringLiteral("\\b(?:name|property)\\s*=\\s*[\"']([^\"']+)[\"']"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression contentRe(QStringLiteral("\\bcontent\\s*=\\s*[\"']([^\"']*)[\"']"), QRegularExpression::CaseInsensitiveOption);
    auto it = meta.globalMatch(head);
    while (it.hasNext()) {
        const QString tag = it.next().captured();
        const auto n = nameRe.match(tag);
        if (!n.hasMatch() || !names.contains(n.captured(1).toLower())) continue;
        const auto c = contentRe.match(tag);
        if (c.hasMatch()) return plainText(c.captured(1));
    }
    return {};
}

}  // namespace

namespace html {

QString decodePage(const QByteArray& data)
{
    if (data.startsWith("\xEF\xBB\xBF") || data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF")) return convert::decodeText(data);
    const QString head = QString::fromLatin1(data.left(4096));
    static const QRegularExpression charset(QStringLiteral("charset\\s*=\\s*[\"']?([A-Za-z0-9_.:-]+)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression xmlEnc(QStringLiteral("<\\?xml[^>]*encoding\\s*=\\s*[\"']([A-Za-z0-9_.:-]+)"), QRegularExpression::CaseInsensitiveOption);
    QString enc;
    if (const auto m = charset.match(head); m.hasMatch()) enc = m.captured(1);
    else if (const auto x = xmlEnc.match(head); x.hasMatch()) enc = x.captured(1);
    QString text = enc.isEmpty() ? convert::decodeText(data) : convert::decodeAs(data, enc);
    // The page is UTF-8 from now on: its declarations say so too.
    if (!enc.isEmpty()) {
        static const QRegularExpression decl(QStringLiteral("(charset\\s*=\\s*[\"']?|encoding\\s*=\\s*[\"'])[A-Za-z0-9_.:-]+"),
                                             QRegularExpression::CaseInsensitiveOption);
        const qsizetype headEnd = qMin<qsizetype>(text.size(), 4096);
        QString h = text.left(headEnd);
        h.replace(decl, QStringLiteral("\\1utf-8"));
        text = h + text.mid(headEnd);
    }
    return text;
}

void readOpfMetadata(MemoryBook& book, const QByteArray& opf)
{
    const auto doc = dom::parse(opf, dom::ParseMode::Xml);
    std::function<void(const dom::Node*)> walk = [&](const dom::Node* n) {
        const QString text = n->textContent().simplified();
        if (n->is("dc:title") && book.metadata.title.isEmpty()) book.metadata.title = text;
        else if (n->is("dc:creator") && !text.isEmpty()) book.metadata.authors << text;
        else if (n->is("dc:language") && book.metadata.language.isEmpty()) book.metadata.language = text;
        else if (n->is("dc:description") && book.metadata.description.isEmpty()) book.metadata.description = plainText(text);
        else if (n->is("dc:publisher") && book.metadata.publisher.isEmpty()) book.metadata.publisher = text;
        else if (n->is("dc:identifier") && book.metadata.identifier.isEmpty()) book.metadata.identifier = text;
        for (const auto& c : n->children)
            if (c->isElement()) walk(c.get());
    };
    walk(doc->root.get());
}

void addPage(MemoryBook& book, const QByteArray& data, const QString& virtualPath)
{
    const QString text = decodePage(data);
    // The head (kept for every part: its styles and links) and the body.
    static const QRegularExpression bodyOpen(QStringLiteral("<body\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    const auto bo = bodyOpen.match(text);
    QString head, bodyTag, body;
    if (bo.hasMatch()) {
        head = text.left(bo.capturedStart());
        bodyTag = bo.captured();
        qsizetype end = text.lastIndexOf(QLatin1String("</body"), -1, Qt::CaseInsensitive);
        if (end < bo.capturedEnd()) end = text.size();
        body = text.mid(bo.capturedEnd(), end - bo.capturedEnd());
    } else {
        head = QStringLiteral("<html><head><meta charset=\"utf-8\"/></head>");
        bodyTag = QStringLiteral("<body>");
        body = text;
    }

    // Metadata from the page, when the book has none yet.
    if (book.metadata.title.isEmpty()) {
        QString title = metaContent(head, {"dc.title", "dcterms.title", "og:title", "title"});
        if (title.isEmpty()) {
            static const QRegularExpression t(QStringLiteral("<title[^>]*>(.*?)</title>"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
            if (const auto m = t.match(head); m.hasMatch()) title = plainText(m.captured(1));
        }
        // "The Project Gutenberg eBook of Ratón Pérez, by Luis Coloma" -> "Ratón Pérez".
        static const QRegularExpression pg(QStringLiteral("^The Project Gutenberg e?Book,? of (.*?)(?:, by .*)?$"), QRegularExpression::CaseInsensitiveOption);
        if (const auto m = pg.match(title); m.hasMatch()) title = m.captured(1).trimmed();
        book.metadata.title = title;
    }
    if (book.metadata.authors.isEmpty()) {
        QString a = metaContent(head, {"dc.creator", "dcterms.creator", "author", "dc.creator.aut"});
        // Library catalogue order ("Coloma, Luis, 1851-1915") -> "Luis Coloma".
        static const QRegularExpression catalogue(QStringLiteral("^([^,]+),\\s*([^,\\d]+?)(?:,\\s*[\\d?]{1,4}\\s*-\\s*[\\d?]{0,4}.*)?$"));
        if (const auto m = catalogue.match(a); m.hasMatch()) a = m.captured(2).trimmed() + QLatin1Char(' ') + m.captured(1).trimmed();
        if (!a.isEmpty()) book.metadata.authors << a;
    }
    if (book.metadata.language.isEmpty()) {
        static const QRegularExpression lang(QStringLiteral("<html\\b[^>]*\\b(?:xml:)?lang\\s*=\\s*[\"']([A-Za-z-]+)[\"']"), QRegularExpression::CaseInsensitiveOption);
        if (const auto m = lang.match(head); m.hasMatch()) book.metadata.language = m.captured(1);
        if (book.metadata.language.isEmpty()) book.metadata.language = metaContent(head, {"dc.language", "dcterms.language", "language"});
        if (book.metadata.language.isEmpty()) book.metadata.language = convert::guessLanguage(plainText(body.left(80000)));
    }

    // The parts.
    std::vector<qsizetype> starts = chapterStarts(body);
    std::vector<QString> parts;
    qsizetype from = 0;
    for (const qsizetype s : starts) {
        if (!plainText(body.mid(from, s - from)).isEmpty() || parts.empty()) parts.push_back(body.mid(from, s - from));
        else parts.back() += body.mid(from, s - from);
        from = s;
    }
    parts.push_back(body.mid(from));
    if (parts.size() > 1 && plainText(parts.front()).isEmpty()) {
        parts[1] = parts.front() + parts[1];
        parts.erase(parts.begin());
    }

    const QString dir = css::dirOf(virtualPath);
    const QString base = QFileInfo(virtualPath).completeBaseName();
    const QString fileName = QFileInfo(virtualPath).fileName();
    auto partPath = [&](size_t i) {
        return parts.size() == 1 ? virtualPath : dir + base + QStringLiteral(".part%1.html").arg(int(i + 1), 4, 10, QLatin1Char('0'));
    };
    // Which part holds each id, so links to it reach the right part.
    QHash<QString, size_t> idPart;
    static const QRegularExpression idRe(QStringLiteral("\\b(?:id|name)\\s*=\\s*[\"']([^\"']+)[\"']"), QRegularExpression::CaseInsensitiveOption);
    for (size_t i = 0; i < parts.size(); ++i) {
        auto it = idRe.globalMatch(parts[i]);
        while (it.hasNext()) {
            const QString id = it.next().captured(1);
            if (!idPart.contains(id)) idPart.insert(id, i);
        }
    }
    const QRegularExpression hrefRe(QStringLiteral("\\bhref\\s*=\\s*([\"'])(?:%1)?#([^\"']+)\\1").arg(QRegularExpression::escape(fileName)),
                                    QRegularExpression::CaseInsensitiveOption);
    int minLevel = 9;
    std::vector<QPair<QString, int>> headings;
    for (const QString& p : parts) {
        headings.push_back(firstHeading(p));
        if (!headings.back().first.isEmpty()) minLevel = qMin(minLevel, headings.back().second);
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        QString p = parts[i];
        if (parts.size() > 1) {
            QString out;
            qsizetype last = 0;
            auto it = hrefRe.globalMatch(p);
            while (it.hasNext()) {
                const auto m = it.next();
                const QString id = m.captured(2);
                const auto at = idPart.constFind(id);
                out += p.mid(last, m.capturedStart() - last);
                const QString target = at == idPart.cend() || *at == i ? QString() : QFileInfo(partPath(*at)).fileName();
                out += QStringLiteral("href=\"%1#%2\"").arg(target, id);
                last = m.capturedEnd();
            }
            p = out + p.mid(last);
        }
        const QString html = head + bodyTag + p + QStringLiteral("</body></html>\n");
        const QString title = headings[i].first;
        const int depth = title.isEmpty() ? 0 : qMax(0, headings[i].second - minLevel);
        book.addChapter(partPath(i), html.toUtf8(), parts.size() > 1 ? title : QString(), depth);
    }
}

}  // namespace html

bool HtmlFormat::canOpen(const QString& path, const QByteArray& head) const
{
    if (!hasSuffix(path, {"html", "htm", "xhtml", "xht", "shtml"})) return false;
    return !head.contains('\0') || head.startsWith("\xFF\xFE") || head.startsWith("\xFE\xFF");
}

std::shared_ptr<Book> HtmlFormat::open(const QString& path, QString* error) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The file can't be read: %1").arg(f.errorString());
        return nullptr;
    }
    auto book = std::make_shared<MemoryBook>();
    book->filePath = path;
    const QFileInfo fi(path);
    book->diskRoot = fi.absolutePath();   // its pictures and style sheets
    html::addPage(*book, f.readAll(), fi.fileName());
    if (book->metadata.title.isEmpty()) book->metadata.title = fi.completeBaseName();
    return book;
}

bool HtmlzFormat::canOpen(const QString& path, const QByteArray& head) const
{
    return hasSuffix(path, {"htmlz"}) && head.startsWith("PK\x03\x04");
}

std::shared_ptr<Book> HtmlzFormat::open(const QString& path, QString* error) const
{
    auto book = std::make_shared<MemoryBook>();
    book->filePath = path;
    QString zipError;
    if (!book->zip.open(path, &zipError)) {
        if (error) *error = zipError;
        return nullptr;
    }
    book->zipOpen = true;
    bool ok = false;
    const QByteArray opf = book->zip.read(QStringLiteral("metadata.opf"), &ok);
    if (ok) {
        html::readOpfMetadata(*book, opf);
        // calibre names the cover in the OPF's guide; it is usually cover.jpg.
        for (const QString& n : book->zip.names())
            if (QFileInfo(n).completeBaseName().compare(QLatin1String("cover"), Qt::CaseInsensitive) == 0 && !convert::imageType(book->zip.read(n)).isEmpty()) {
                book->metadata.coverPath = n;
                break;
            }
    }
    QString page;
    for (const QString& n : book->zip.names()) {
        const QString s = QFileInfo(n).suffix().toLower();
        if (s == QLatin1String("html") || s == QLatin1String("htm") || s == QLatin1String("xhtml")) {
            if (page.isEmpty() || QFileInfo(n).completeBaseName().compare(QLatin1String("index"), Qt::CaseInsensitive) == 0) page = n;
        }
    }
    if (page.isEmpty()) {
        if (error) *error = QStringLiteral("The HTMLZ file holds no page.");
        return nullptr;
    }
    html::addPage(*book, book->zip.read(page), page);
    if (book->metadata.title.isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();
    return book;
}

bool ZipBookFormat::canOpen(const QString& path, const QByteArray& head) const
{
    return hasSuffix(path, {"zip"}) && head.startsWith("PK\x03\x04");
}

std::shared_ptr<Book> ZipBookFormat::open(const QString& path, QString* error) const
{
    ZipArchive probe;
    QString zipError;
    if (!probe.open(path, &zipError)) {
        if (error) *error = zipError;
        return nullptr;
    }
    QString page, textFile, fb2, epub;
    qint64 pageSize = -1;
    int pictures = 0;
    for (const QString& n : probe.names()) {
        if (n.startsWith(QLatin1String("__MACOSX/"))) continue;
        const QString s = QFileInfo(n).suffix().toLower();
        if (s == QLatin1String("html") || s == QLatin1String("htm") || s == QLatin1String("xhtml")) {
            // The book's page: the largest one (a "-h.zip" also holds small pages for its notes).
            const qint64 size = probe.read(n).size();
            if (size > pageSize) {
                page = n;
                pageSize = size;
            }
        } else if (s == QLatin1String("txt") || s == QLatin1String("md") || s == QLatin1String("markdown")) {
            if (textFile.isEmpty()) textFile = n;
        } else if (s == QLatin1String("fb2")) {
            fb2 = n;
        } else if (s == QLatin1String("epub")) {
            epub = n;
        } else if (QStringList{"jpg", "jpeg", "png", "gif", "webp", "bmp"}.contains(s)) {
            ++pictures;
        }
    }
    if (!fb2.isEmpty()) return Fb2Format().open(path, error);
    if (!page.isEmpty()) {
        auto book = std::make_shared<MemoryBook>();
        book->filePath = path;
        if (!book->zip.open(path, &zipError)) {
            if (error) *error = zipError;
            return nullptr;
        }
        book->zipOpen = true;
        html::addPage(*book, book->zip.read(page), page);
        if (book->metadata.title.isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();
        // A picture named cover, if there is one.
        for (const QString& n : book->zip.names())
            if (QFileInfo(n).completeBaseName().compare(QLatin1String("cover"), Qt::CaseInsensitive) == 0 && !convert::imageType(book->zip.read(n)).isEmpty()) {
                book->metadata.coverPath = n;
                break;
            }
        return book;
    }
    if (!textFile.isEmpty()) {
        auto book = std::make_shared<MemoryBook>();
        book->filePath = path;
        book->zip.open(path, &zipError);
        book->zipOpen = true;
        const QString text = convert::decodeText(book->zip.read(textFile));
        const QString s = QFileInfo(textFile).suffix().toLower();
        if (s == QLatin1String("txt")) text::fromPlainText(*book, text);
        else text::fromMarkdown(*book, text);
        if (book->metadata.title.isEmpty()) book->metadata.title = QFileInfo(textFile).completeBaseName();
        return book;
    }
    if (pictures > 0) return ComicFormat().open(path, error);
    if (error)
        *error = !epub.isEmpty() ? QStringLiteral("The zip holds an EPUB book: unzip it, then add the EPUB file.")
                                 : QStringLiteral("The zip holds no book FreeReader can open.");
    return nullptr;
}

}  // namespace core

#include "Fb2Format.h"

#include "core/format/Convert.h"
#include "core/html/HtmlParser.h"
#include "core/zip/ZipArchive.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace core {

namespace {

using convert::escape;

QString hrefOf(const dom::Node* n)
{
    for (const auto& a : n->attrs)
        if (a.first == QLatin1String("href") || a.first.endsWith(QLatin1String(":href"))) return a.second;
    return {};
}

QString imagePath(const QString& id)
{
    QString clean = id;
    clean.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    return QStringLiteral("images/") + clean;
}

struct Converter {
    MemoryBook& book;
    QHash<QString, QString> idFile;   // element id -> the chapter file it is in
    QString currentFile;
    QStringList notesFiles;

    QString idAttr(const dom::Node* n)
    {
        const QString id = n->id();
        if (id.isEmpty()) return {};
        idFile.insert(id, currentFile);
        return QStringLiteral(" id=\"%1\"").arg(escape(id));
    }

    QString inlineHtml(const dom::Node* n)
    {
        QString s;
        for (const auto& c : n->children) {
            if (c->isText()) {
                s += escape(c->text);
                continue;
            }
            const QString& name = c->name;
            const QString inner = inlineHtml(c.get());
            if (name == QLatin1String("strong")) s += QStringLiteral("<strong>") + inner + QStringLiteral("</strong>");
            else if (name == QLatin1String("emphasis")) s += QStringLiteral("<em>") + inner + QStringLiteral("</em>");
            else if (name == QLatin1String("strikethrough")) s += QStringLiteral("<del>") + inner + QStringLiteral("</del>");
            else if (name == QLatin1String("sub") || name == QLatin1String("sup")) s += QStringLiteral("<%1>%2</%1>").arg(name, inner);
            else if (name == QLatin1String("code")) s += QStringLiteral("<code>") + inner + QStringLiteral("</code>");
            else if (name == QLatin1String("a")) {
                const QString href = hrefOf(c.get());
                const bool note = c->attr("type") == QLatin1String("note");
                QString a = QStringLiteral("<a href=\"%1\"%2>%3</a>").arg(escape(href), note ? QStringLiteral(" class=\"noteref\" epub:type=\"noteref\"") : QString(), inner);
                s += note && !inner.contains(QLatin1String("<sup")) ? QStringLiteral("<sup>") + a + QStringLiteral("</sup>") : a;
            } else if (name == QLatin1String("image")) {
                s += QStringLiteral("<img src=\"%1\" alt=\"\"/>").arg(escape(imagePath(hrefOf(c.get()).mid(1))));
            } else {
                s += inner;   // style and unknown inline elements: their text
            }
        }
        return s;
    }

    QString titleText(const dom::Node* title)
    {
        QStringList lines;
        for (const auto& c : title->children)
            if (c->isElement() && c->is("p")) lines << c->textContent().simplified();
        if (lines.isEmpty()) lines << title->textContent().simplified();
        lines.removeAll(QString());
        return lines.join(QStringLiteral(". "));
    }

    QString titleHtml(const dom::Node* title, int level, const QString& extraId = {})
    {
        QStringList lines;
        for (const auto& c : title->children)
            if (c->isElement() && c->is("p")) lines << inlineHtml(c.get());
        if (lines.isEmpty()) lines << escape(title->textContent().simplified());
        const int h = qBound(1, level, 6);
        const QString id = extraId.isEmpty() ? QString() : QStringLiteral(" id=\"%1\"").arg(extraId);
        return QStringLiteral("<h%1%2>%3</h%1>\n").arg(h).arg(id, lines.join(QStringLiteral("<br/>")));
    }

    QString blocks(const dom::Node* n, int level, int tocDepth, const QString& chapterFile)
    {
        QString s;
        for (const auto& cp : n->children) {
            const dom::Node* c = cp.get();
            if (!c->isElement()) continue;
            const QString& name = c->name;
            if (name == QLatin1String("p")) s += QStringLiteral("<p%1>%2</p>\n").arg(idAttr(c), inlineHtml(c));
            else if (name == QLatin1String("empty-line")) s += QStringLiteral("<p class=\"empty\">&#160;</p>\n");
            else if (name == QLatin1String("subtitle")) s += QStringLiteral("<h%1 class=\"subtitle\"%2>%3</h%1>\n").arg(qMin(6, level + 1)).arg(idAttr(c), inlineHtml(c));
            else if (name == QLatin1String("title")) s += titleHtml(c, level);
            else if (name == QLatin1String("epigraph")) s += QStringLiteral("<blockquote class=\"epigraph\"%1>\n").arg(idAttr(c)) + blocks(c, level, tocDepth, chapterFile) + QStringLiteral("</blockquote>\n");
            else if (name == QLatin1String("cite")) s += QStringLiteral("<blockquote%1>\n").arg(idAttr(c)) + blocks(c, level, tocDepth, chapterFile) + QStringLiteral("</blockquote>\n");
            else if (name == QLatin1String("annotation")) s += QStringLiteral("<div class=\"annotation\">\n") + blocks(c, level, tocDepth, chapterFile) + QStringLiteral("</div>\n");
            else if (name == QLatin1String("text-author")) s += QStringLiteral("<p class=\"author\" style=\"text-align:right;font-style:italic\">%1</p>\n").arg(inlineHtml(c));
            else if (name == QLatin1String("date")) s += QStringLiteral("<p class=\"date\" style=\"text-align:right\">%1</p>\n").arg(inlineHtml(c));
            else if (name == QLatin1String("poem")) s += QStringLiteral("<div class=\"poem\"%1>\n").arg(idAttr(c)) + blocks(c, level, tocDepth, chapterFile) + QStringLiteral("</div>\n");
            else if (name == QLatin1String("stanza")) {
                s += QStringLiteral("<p class=\"stanza\">");
                bool first = true;
                for (const auto& v : c->children) {
                    if (!v->isElement()) continue;
                    if (v->is("title")) {
                        s += QStringLiteral("<strong>") + escape(titleText(v.get())) + QStringLiteral("</strong><br/>");
                        continue;
                    }
                    if (!v->is("v")) continue;
                    if (!first) s += QStringLiteral("<br/>");
                    s += inlineHtml(v.get());
                    first = false;
                }
                s += QStringLiteral("</p>\n");
            } else if (name == QLatin1String("image")) {
                s += QStringLiteral("<div class=\"image\"%1><img src=\"%2\" alt=\"\"/></div>\n").arg(idAttr(c), escape(imagePath(hrefOf(c).mid(1))));
            } else if (name == QLatin1String("table")) {
                s += QStringLiteral("<table>");
                for (const auto& tr : c->children) {
                    if (!tr->isElement() || !tr->is("tr")) continue;
                    s += QStringLiteral("<tr>");
                    for (const auto& td : tr->children)
                        if (td->isElement() && (td->is("td") || td->is("th"))) s += QStringLiteral("<%1>%2</%1>").arg(td->name, inlineHtml(td.get()));
                    s += QStringLiteral("</tr>");
                }
                s += QStringLiteral("</table>\n");
            } else if (name == QLatin1String("section")) {
                // A nested section: its title is a heading with an entry in the contents.
                QString inner;
                QString anchor = c->id();
                if (const dom::Node* t = c->firstElement("title")) {
                    if (anchor.isEmpty()) anchor = QStringLiteral("s%1").arg(book.toc.size() + 1);
                    book.addTocEntry(titleText(t), chapterFile + QLatin1Char('#') + anchor, tocDepth);
                    idFile.insert(anchor, currentFile);
                }
                s += QStringLiteral("<div class=\"section\"%1>\n").arg(anchor.isEmpty() ? QString() : QStringLiteral(" id=\"%1\"").arg(escape(anchor)));
                if (!anchor.isEmpty()) idFile.insert(anchor, currentFile);
                // The title inside the section is written by the recursive call (as an h at the next level).
                inner = blocks(c, level + 1, tocDepth + 1, chapterFile);
                s += inner + QStringLiteral("</div>\n");
            }
        }
        return s;
    }
};

// FB2's XML declaration often names windows-1251 (Russian) or windows-1252.
QString decodeFb2(const QByteArray& data)
{
    if (data.startsWith("\xEF\xBB\xBF") || data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF")) return convert::decodeText(data);
    static const QRegularExpression enc(QStringLiteral("<\\?xml[^>]*encoding\\s*=\\s*[\"']([A-Za-z0-9_.:-]+)"), QRegularExpression::CaseInsensitiveOption);
    const auto m = enc.match(QString::fromLatin1(data.left(300)));
    return m.hasMatch() ? convert::decodeAs(data, m.captured(1)) : convert::decodeText(data);
}

}  // namespace

bool Fb2Format::canOpen(const QString& path, const QByteArray& head) const
{
    const QString lower = QFileInfo(path).fileName().toLower();
    if (lower.endsWith(QLatin1String(".fb2")) || lower.endsWith(QLatin1String(".fbz")) || lower.endsWith(QLatin1String(".fb2.zip"))) return true;
    return head.contains("<FictionBook");
}

std::shared_ptr<Book> Fb2Format::open(const QString& path, QString* error) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The file can't be read: %1").arg(f.errorString());
        return nullptr;
    }
    QByteArray data = f.readAll();
    if (data.startsWith("PK\x03\x04")) {   // .fbz / .fb2.zip
        ZipArchive zip;
        QString zipError;
        if (!zip.openData(data, &zipError)) {
            if (error) *error = zipError;
            return nullptr;
        }
        QString inner;
        for (const QString& n : zip.names())
            if (n.endsWith(QLatin1String(".fb2"), Qt::CaseInsensitive)) {
                inner = n;
                break;
            }
        if (inner.isEmpty()) {
            if (error) *error = QStringLiteral("The zip holds no FB2 book.");
            return nullptr;
        }
        data = zip.read(inner);
    }
    const auto doc = dom::parseText(decodeFb2(data), dom::ParseMode::Xml);
    const dom::Node* root = doc->documentElement();
    if (!root || root->name != QLatin1String("fictionbook")) {
        if (error) *error = QStringLiteral("The book is damaged: it isn't a FictionBook file.");
        return nullptr;
    }
    auto book = std::make_shared<MemoryBook>();
    book->filePath = path;

    // Description: title, authors, language, annotation, cover, series, identifier.
    if (const dom::Node* desc = root->firstElement("description")) {
        if (const dom::Node* ti = desc->firstElement("title-info")) {
            if (const dom::Node* t = ti->firstElement("book-title")) book->metadata.title = t->textContent().simplified();
            for (const dom::Node* a : ti->elements("author")) {
                QStringList parts;
                for (const char* p : {"first-name", "middle-name", "last-name"})
                    if (const dom::Node* x = a->firstElement(p)) parts << x->textContent().simplified();
                parts.removeAll(QString());
                if (parts.isEmpty())
                    if (const dom::Node* nick = a->firstElement("nickname")) parts << nick->textContent().simplified();
                if (!parts.isEmpty()) book->metadata.authors << parts.join(QLatin1Char(' '));
            }
            if (const dom::Node* l = ti->firstElement("lang")) book->metadata.language = l->textContent().simplified();
            if (const dom::Node* an = ti->firstElement("annotation")) book->metadata.description = an->textContent().simplified();
            if (const dom::Node* cover = ti->firstElement("coverpage"))
                if (const dom::Node* img = cover->firstElement("image")) book->metadata.coverPath = imagePath(hrefOf(img).mid(1));
            if (const dom::Node* seq = ti->firstElement("sequence")) {
                book->metadata.series = seq->attr("name");
                book->metadata.seriesNumber = seq->attr("number");
            }
        }
        if (const dom::Node* pi = desc->firstElement("publish-info")) {
            if (const dom::Node* isbn = pi->firstElement("isbn")) book->metadata.identifier = isbn->textContent().simplified();
            if (const dom::Node* pub = pi->firstElement("publisher")) book->metadata.publisher = pub->textContent().simplified();
        }
        if (book->metadata.identifier.isEmpty())
            if (const dom::Node* di = desc->firstElement("document-info"))
                if (const dom::Node* id = di->firstElement("id")) book->metadata.identifier = id->textContent().simplified();
    }
    if (book->metadata.title.isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();

    // Pictures.
    for (const dom::Node* bin : root->elements("binary")) {
        const QByteArray bytes = QByteArray::fromBase64(bin->textContent().toLatin1(), QByteArray::Base64Encoding);
        if (!bytes.isEmpty()) book->addFile(imagePath(bin->id()), bytes);
    }
    if (!book->metadata.coverPath.isEmpty() && !book->files.contains(book->metadata.coverPath)) book->metadata.coverPath.clear();

    // Chapters: the main body's top-level sections; then the notes.
    Converter cv{*book, {}, {}, {}};
    struct Chapter {
        QString file, title, html;
    };
    std::vector<Chapter> chapters;
    const QString lang = book->metadata.language;
    int n = 0;
    auto newFile = [&] { return QStringLiteral("ch%1.html").arg(++n, 4, 10, QLatin1Char('0')); };
    for (const dom::Node* body : root->elements("body")) {
        const QString bodyName = body->attr("name").toLower();
        const bool notes = !bodyName.isEmpty();
        if (notes) {
            Chapter ch;
            ch.file = newFile();
            cv.currentFile = ch.file;
            ch.title = bodyName == QLatin1String("notes") ? QStringLiteral("Notes") : (body->firstElement("title") ? cv.titleText(body->firstElement("title")) : bodyName);
            ch.html = cv.blocks(body, 2, 1, ch.file);
            chapters.push_back(ch);
            continue;
        }
        // Before the first section: the book's own title and epigraph.
        QString front;
        Chapter pending;
        pending.file = newFile();
        cv.currentFile = pending.file;
        for (const auto& cp : body->children) {
            const dom::Node* c = cp.get();
            if (!c->isElement()) continue;
            if (!c->is("section")) {
                // The front matter (the book's title, an epigraph, a picture), in order.
                if (c->is("title")) front += cv.titleHtml(c, 1);
                else if (c->is("p")) front += QStringLiteral("<p>%1</p>\n").arg(cv.inlineHtml(c));
                else if (c->is("epigraph")) front += QStringLiteral("<blockquote class=\"epigraph\">\n") + cv.blocks(c, 2, 1, pending.file) + QStringLiteral("</blockquote>\n");
                else if (c->is("image")) front += QStringLiteral("<div class=\"image\"><img src=\"%1\" alt=\"\"/></div>\n").arg(escape(imagePath(hrefOf(c).mid(1))));
                else front += cv.blocks(c, 2, 1, pending.file);
                continue;
            }
            if (!front.trimmed().isEmpty()) {
                if (pending.file.isEmpty()) pending.file = newFile();
                pending.html = front;
                chapters.push_back(pending);
                front.clear();
                pending = Chapter();
            }
            Chapter ch;
            ch.file = pending.file.isEmpty() ? newFile() : pending.file;
            pending.file.clear();
            cv.currentFile = ch.file;
            if (!c->id().isEmpty()) cv.idFile.insert(c->id(), ch.file);
            const dom::Node* t = c->firstElement("title");
            ch.title = t ? cv.titleText(t) : QString();
            const QString id = c->id().isEmpty() ? QString() : QStringLiteral(" id=\"%1\"").arg(escape(c->id()));
            ch.html = QStringLiteral("<div class=\"section\"%1>\n").arg(id) + cv.blocks(c, 2, 1, ch.file) + QStringLiteral("</div>\n");
            chapters.push_back(ch);
        }
        if (!front.trimmed().isEmpty()) {
            if (pending.file.isEmpty()) pending.file = newFile();
            pending.html = front;
            chapters.push_back(pending);
        }
    }
    // Links: "#id" -> the chapter holding that id.
    static const QRegularExpression href(QStringLiteral("href=\"#([^\"]+)\""));
    // Nested-section contents entries were added while converting; the chapters' own entries go first in order.
    std::vector<TocEntry> nested = book->toc;
    book->toc.clear();
    for (const Chapter& ch : chapters) {
        QString html = ch.html;
        QString out;
        qsizetype last = 0;
        auto it = href.globalMatch(html);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString target = cv.idFile.value(m.captured(1));
            out += html.mid(last, m.capturedStart() - last);
            out += target.isEmpty() || target == ch.file ? m.captured() : QStringLiteral("href=\"%1#%2\"").arg(target, m.captured(1));
            last = m.capturedEnd();
        }
        html = out + html.mid(last);
        book->addChapter(ch.file, convert::page(ch.title.isEmpty() ? book->metadata.title : ch.title, html, lang), ch.title, 0);
        for (const TocEntry& e : nested)
            if (e.href.startsWith(ch.file + QLatin1Char('#'))) book->addTocEntry(e.title, e.href, e.depth);
    }
    if (book->spine.empty()) {
        if (error) *error = QStringLiteral("The book has no chapters to show.");
        return nullptr;
    }
    if (book->metadata.language.isEmpty()) book->metadata.language = convert::guessLanguage(root->textContent().left(60000));
    return book;
}

}  // namespace core

#include "TextFormats.h"

#include "core/format/Convert.h"
#include "core/html/HtmlParser.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace core {

namespace {

using convert::escape;

bool hasSuffix(const QString& path, std::initializer_list<const char*> suffixes)
{
    const QString s = QFileInfo(path).suffix().toLower();
    for (const char* x : suffixes)
        if (s == QLatin1String(x)) return true;
    return false;
}

// Text that is mostly letters, digits, spaces and punctuation: not a binary file named .txt.
bool looksLikeText(const QByteArray& head)
{
    if (head.isEmpty()) return true;
    if (head.startsWith("\xFF\xFE") || head.startsWith("\xFE\xFF") || head.startsWith("\xEF\xBB\xBF")) return true;
    int control = 0;
    for (const char c : head) {
        const uchar u = uchar(c);
        if (u == 0) return false;
        if (u < 0x20 && u != '\n' && u != '\r' && u != '\t' && u != '\f') ++control;
    }
    return control * 50 < head.size();
}

QByteArray readFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The file can't be read: %1").arg(f.errorString());
        return {};
    }
    return f.readAll();
}

// Plain text in chapters of at most about this many characters when the text has no headings.
constexpr int kChunk = 60000;

struct Block {
    QStringList lines;
    int blankBefore = 0;
};

const QRegularExpression& headingRe()
{
    static const QRegularExpression re(
        QStringLiteral("^(cap[ií]tulo|chapter|chapitre|kapitel|capitolo|cap\\.|parte|part|partie|libro|book|livre|tratado|canto|acto|act|"
                       "jornada|escena|scene|pr[oó]logo|prologue|ep[ií]logo|epilogue|introducci[oó]n|introduction|prefacio|preface|pr[eé]face|"
                       "ap[eé]ndice|appendix|conclusi[oó]n|conclusion|dedicatoria)\\b.{0,70}$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

bool isRoman(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^[IVXLCDM]{1,7}\\.?$"));
    return re.match(s).hasMatch();
}

bool isAllCaps(const QString& s)
{
    int letters = 0;
    for (const QChar c : s) {
        if (c.isLower()) return false;
        if (c.isLetter()) ++letters;
    }
    return letters >= 3;
}

}  // namespace

// ---------------------------------------------------------------- plain text

namespace text {

void fromPlainText(MemoryBook& book, const QString& input)
{
    QString t = input;
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    t.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    t.replace(QLatin1Char('\f'), QLatin1String("\n\n"));
    const QStringList lines = t.split(QLatin1Char('\n'));

    // Project Gutenberg and similar headers: "Title:", "Author:", "Language:".
    for (int i = 0; i < qMin<int>(int(lines.size()), 120); ++i) {
        const QString l = lines[i].trimmed();
        auto field = [&](const char* en, const char* es) -> QString {
            for (const char* key : {en, es}) {
                const QString k = QString::fromUtf8(key);
                if (l.startsWith(k, Qt::CaseInsensitive)) return l.mid(k.size()).trimmed();
            }
            return {};
        };
        if (book.metadata.title.isEmpty()) book.metadata.title = field("Title:", "Título:");
        if (book.metadata.authors.isEmpty()) {
            const QString a = field("Author:", "Autor:");
            if (!a.isEmpty()) book.metadata.authors << a;
        }
        if (book.metadata.language.isEmpty()) {
            const QString lang = field("Language:", "Idioma:").toLower();
            static const QHash<QString, QString> codes = {{"spanish", "es"}, {"español", "es"}, {"english", "en"}, {"french", "fr"},
                                                          {"german", "de"}, {"italian", "it"}, {"portuguese", "pt"}};
            if (codes.contains(lang)) book.metadata.language = codes.value(lang);
        }
    }

    if (book.metadata.language.isEmpty()) book.metadata.language = convert::guessLanguage(t);

    // Paragraphs: separated by blank lines when the text has them, else one per line.
    int blank = 0, nonBlank = 0;
    std::vector<int> lengths;
    for (const QString& l : lines) {
        if (l.trimmed().isEmpty()) ++blank;
        else {
            ++nonBlank;
            lengths.push_back(int(l.trimmed().size()));
        }
    }
    const bool blankSeparated = blank * 12 >= nonBlank && blank > 0;
    int wrapWidth = 0;
    if (!lengths.empty()) {
        std::sort(lengths.begin(), lengths.end());
        wrapWidth = lengths[size_t(lengths.size() * 9 / 10)];
    }
    std::vector<Block> blocks;
    int pendingBlank = 2;
    for (const QString& raw : lines) {
        const QString l = raw.trimmed();
        if (l.isEmpty()) {
            ++pendingBlank;
            if (blankSeparated && !blocks.empty() && !blocks.back().lines.isEmpty()) blocks.push_back({});
            continue;
        }
        if (!blankSeparated || blocks.empty()) blocks.push_back({});
        Block& b = blocks.back();
        if (b.lines.isEmpty()) b.blankBefore = pendingBlank;
        b.lines << l;
        pendingBlank = 0;
    }
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [](const Block& b) { return b.lines.isEmpty(); }), blocks.end());

    // Headings: a short block that names a chapter, or a short capitalised line after a gap.
    auto headingOf = [&](const Block& b) -> QString {
        if (b.lines.size() > 2) return {};
        // Decoration around a heading ("#CAPÍTULO I#", "* * PARTE II * *", "=== I ===") isn't part of it.
        static const QRegularExpression deco(QStringLiteral("^[#*=_~\\s]+|[#*=_~\\s]+$"));
        const QString first = QString(b.lines.first()).remove(deco);
        const QString all = QString(b.lines.join(QLatin1Char(' '))).remove(deco);
        if (first.isEmpty()) return {};
        if (all.size() > 90) return {};
        if (headingRe().match(first).hasMatch() || isRoman(first)) return all;
        if (b.blankBefore >= 3 && isAllCaps(all) && all.size() <= 60) return all;
        return {};
    };
    std::vector<int> headingAt;   // block index of each heading
    for (int i = 0; i < int(blocks.size()); ++i)
        if (!headingOf(blocks[size_t(i)]).isEmpty()) headingAt.push_back(i);
    const bool useHeadings = headingAt.size() >= 2 && headingAt.size() < blocks.size() / 2 + 2 && headingAt.size() <= 1500;

    const QString lang = book.metadata.language;
    int part = 0;
    QString body, chapterTitle;
    int bodyChars = 0;
    auto flush = [&](const QString& nextTitle) {
        if (!body.isEmpty()) {
            ++part;
            const QString path = QStringLiteral("text/part%1.html").arg(part, 4, 10, QLatin1Char('0'));
            book.addChapter(path, convert::page(chapterTitle.isEmpty() ? book.metadata.title : chapterTitle, body, lang), chapterTitle);
        }
        body.clear();
        bodyChars = 0;
        chapterTitle = nextTitle;
    };
    for (int i = 0; i < int(blocks.size()); ++i) {
        const Block& b = blocks[size_t(i)];
        const QString heading = useHeadings && std::find(headingAt.begin(), headingAt.end(), i) != headingAt.end() ? headingOf(b) : QString();
        if (!heading.isEmpty()) {
            flush(heading);
            body += QStringLiteral("<h2>") + escape(heading) + QStringLiteral("</h2>\n");
            continue;
        }
        if (!useHeadings && bodyChars > kChunk) flush(QString());
        // A poem (or an address, a list): every line clearly shorter than the text's usual width.
        int longest = 0;
        for (const QString& l : b.lines) longest = qMax(longest, int(l.size()));
        const bool keepBreaks = b.lines.size() >= 2 && wrapWidth >= 50 && longest < wrapWidth * 7 / 10;
        QString p;
        for (int k = 0; k < b.lines.size(); ++k) {
            if (k > 0) p += keepBreaks ? QStringLiteral("<br/>") : QStringLiteral(" ");
            p += escape(b.lines[k]);
        }
        body += (keepBreaks ? QStringLiteral("<p class=\"lines\">") : QStringLiteral("<p>")) + p + QStringLiteral("</p>\n");
        bodyChars += int(p.size());
    }
    flush(QString());
    if (book.spine.empty()) book.addChapter(QStringLiteral("text/part0001.html"), convert::page(book.metadata.title, QString(), lang));
}

// ---------------------------------------------------------------- Markdown

QString markdownInline(const QString& input)
{
    // Code spans first, kept aside so their text isn't read as other marks.
    QStringList codes;
    QString s;
    {
        static const QRegularExpression code(QStringLiteral("(`+)(.+?)\\1"));
        qsizetype last = 0;
        auto it = code.globalMatch(input);
        while (it.hasNext()) {
            const auto m = it.next();
            s += escape(input.mid(last, m.capturedStart() - last));
            s += QStringLiteral("\x01%1\x02").arg(codes.size());
            codes << QStringLiteral("<code>") + escape(m.captured(2).trimmed()) + QStringLiteral("</code>");
            last = m.capturedEnd();
        }
        s += escape(input.mid(last));
    }
    static const QRegularExpression image(QStringLiteral("!\\[([^\\]]*)\\]\\(\\s*([^)\\s]+)(?:\\s+&quot;[^)]*&quot;)?\\s*\\)"));
    s.replace(image, QStringLiteral("<img src=\"\\2\" alt=\"\\1\"/>"));
    static const QRegularExpression link(QStringLiteral("\\[([^\\]]+)\\]\\(\\s*([^)\\s]+)(?:\\s+&quot;[^)]*&quot;)?\\s*\\)"));
    s.replace(link, QStringLiteral("<a href=\"\\2\">\\1</a>"));
    static const QRegularExpression autolink(QStringLiteral("&lt;((?:https?|mailto):[^\\s&]+)&gt;"));
    s.replace(autolink, QStringLiteral("<a href=\"\\1\">\\1</a>"));
    static const QRegularExpression strong(QStringLiteral("(\\*\\*|__)(?=\\S)(.+?)(?<=\\S)\\1"));
    s.replace(strong, QStringLiteral("<strong>\\2</strong>"));
    static const QRegularExpression em(QStringLiteral("(?<![\\w*])\\*(?=\\S)(.+?)(?<=\\S)\\*(?![\\w*])|(?<![\\w_])_(?=\\S)(.+?)(?<=\\S)_(?![\\w_])"));
    {
        QString out;
        qsizetype last = 0;
        auto it = em.globalMatch(s);
        while (it.hasNext()) {
            const auto m = it.next();
            out += s.mid(last, m.capturedStart() - last);
            out += QStringLiteral("<em>") + (m.captured(1).isEmpty() ? m.captured(2) : m.captured(1)) + QStringLiteral("</em>");
            last = m.capturedEnd();
        }
        s = out + s.mid(last);
    }
    static const QRegularExpression strike(QStringLiteral("~~(?=\\S)(.+?)(?<=\\S)~~"));
    s.replace(strike, QStringLiteral("<del>\\1</del>"));
    for (int i = 0; i < codes.size(); ++i) s.replace(QStringLiteral("\x01%1\x02").arg(i), codes[i]);
    return s;
}

namespace {

struct MdBlock {
    enum Kind { Para, Heading, Code, Quote, List, Rule, Html } kind = Para;
    int level = 0;          // heading level; list: 1 ordered
    QStringList lines;
};

std::vector<MdBlock> mdBlocks(const QStringList& lines)
{
    static const QRegularExpression atx(QStringLiteral("^\\s{0,3}(#{1,6})\\s+(.*?)\\s*#*\\s*$"));
    static const QRegularExpression rule(QStringLiteral("^\\s{0,3}([-*_])(\\s*\\1){2,}\\s*$"));
    static const QRegularExpression bullet(QStringLiteral("^\\s{0,3}([-*+]|\\d{1,9}[.)])\\s+(.*)$"));
    static const QRegularExpression fence(QStringLiteral("^\\s{0,3}(```|~~~)"));
    std::vector<MdBlock> out;
    for (int i = 0; i < lines.size(); ++i) {
        const QString& l = lines[i];
        if (l.trimmed().isEmpty()) continue;
        if (const auto f = fence.match(l); f.hasMatch()) {
            MdBlock b{MdBlock::Code, 0, {}};
            const QString marker = f.captured(1);
            for (++i; i < lines.size() && !lines[i].trimmed().startsWith(marker); ++i) b.lines << lines[i];
            out.push_back(b);
            continue;
        }
        if (const auto h = atx.match(l); h.hasMatch()) {
            out.push_back({MdBlock::Heading, int(h.captured(1).size()), {h.captured(2)}});
            continue;
        }
        if (rule.match(l).hasMatch()) {
            out.push_back({MdBlock::Rule, 0, {}});
            continue;
        }
        if (l.startsWith(QLatin1String("    ")) || l.startsWith(QLatin1Char('\t'))) {
            MdBlock b{MdBlock::Code, 0, {}};
            for (; i < lines.size() && (lines[i].startsWith(QLatin1String("    ")) || lines[i].startsWith(QLatin1Char('\t')) || lines[i].trimmed().isEmpty()); ++i)
                b.lines << (lines[i].startsWith(QLatin1Char('\t')) ? lines[i].mid(1) : lines[i].mid(4));
            --i;
            while (!b.lines.isEmpty() && b.lines.last().trimmed().isEmpty()) b.lines.removeLast();
            out.push_back(b);
            continue;
        }
        if (l.trimmed().startsWith(QLatin1Char('>'))) {
            MdBlock b{MdBlock::Quote, 0, {}};
            for (; i < lines.size() && !lines[i].trimmed().isEmpty(); ++i) {
                QString q = lines[i].trimmed();
                if (q.startsWith(QLatin1Char('>'))) q = q.mid(1);
                if (q.startsWith(QLatin1Char(' '))) q = q.mid(1);
                b.lines << q;
            }
            --i;
            out.push_back(b);
            continue;
        }
        if (const auto m = bullet.match(l); m.hasMatch()) {
            MdBlock b{MdBlock::List, m.captured(1).front().isDigit() ? 1 : 0, {}};
            for (; i < lines.size() && !lines[i].trimmed().isEmpty(); ++i) {
                const auto item = bullet.match(lines[i]);
                if (item.hasMatch()) b.lines << item.captured(2);
                else if (!b.lines.isEmpty()) b.lines.last() += QLatin1Char(' ') + lines[i].trimmed();
            }
            --i;
            out.push_back(b);
            continue;
        }
        if (l.trimmed().startsWith(QLatin1Char('<')) && !l.trimmed().startsWith(QLatin1String("<http"))) {
            MdBlock b{MdBlock::Html, 0, {}};
            for (; i < lines.size() && !lines[i].trimmed().isEmpty(); ++i) b.lines << lines[i];
            --i;
            out.push_back(b);
            continue;
        }
        // A paragraph, or a Setext heading (underlined with === or ---).
        MdBlock b{MdBlock::Para, 0, {}};
        for (; i < lines.size() && !lines[i].trimmed().isEmpty(); ++i) {
            const QString t = lines[i].trimmed();
            if (!b.lines.isEmpty() && (QRegularExpression(QStringLiteral("^=+$")).match(t).hasMatch() || QRegularExpression(QStringLiteral("^-+$")).match(t).hasMatch())) {
                b.kind = MdBlock::Heading;
                b.level = t.startsWith(QLatin1Char('=')) ? 1 : 2;
                b.lines = QStringList{b.lines.join(QLatin1Char(' '))};
                ++i;
                break;
            }
            if (!b.lines.isEmpty() && (atx.match(lines[i]).hasMatch() || fence.match(lines[i]).hasMatch())) break;
            b.lines << lines[i];
        }
        --i;
        out.push_back(b);
    }
    return out;
}

QString mdHtml(const std::vector<MdBlock>& blocks);

QString mdBlockHtml(const MdBlock& b)
{
    switch (b.kind) {
    case MdBlock::Heading: {
        const int lv = qBound(1, b.level, 6);
        return QStringLiteral("<h%1>%2</h%1>\n").arg(lv).arg(markdownInline(b.lines.value(0)));
    }
    case MdBlock::Code: return QStringLiteral("<pre><code>") + escape(b.lines.join(QLatin1Char('\n'))) + QStringLiteral("</code></pre>\n");
    case MdBlock::Quote: return QStringLiteral("<blockquote>") + mdHtml(mdBlocks(b.lines)) + QStringLiteral("</blockquote>\n");
    case MdBlock::List: {
        QString s = b.level ? QStringLiteral("<ol>") : QStringLiteral("<ul>");
        for (const QString& item : b.lines) s += QStringLiteral("<li>") + markdownInline(item) + QStringLiteral("</li>");
        return s + (b.level ? QStringLiteral("</ol>\n") : QStringLiteral("</ul>\n"));
    }
    case MdBlock::Rule: return QStringLiteral("<hr/>\n");
    case MdBlock::Html: return b.lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    case MdBlock::Para: {
        QString s;
        for (int i = 0; i < b.lines.size(); ++i) {
            QString l = b.lines[i];
            const bool hardBreak = l.endsWith(QLatin1String("  ")) || l.endsWith(QLatin1Char('\\'));
            l = l.trimmed();
            if (l.endsWith(QLatin1Char('\\'))) l.chop(1);
            s += markdownInline(l);
            if (i + 1 < b.lines.size()) s += hardBreak ? QStringLiteral("<br/>") : QStringLiteral(" ");
        }
        return QStringLiteral("<p>") + s + QStringLiteral("</p>\n");
    }
    }
    return {};
}

QString mdHtml(const std::vector<MdBlock>& blocks)
{
    QString s;
    for (const MdBlock& b : blocks) s += mdBlockHtml(b);
    return s;
}

}  // namespace

void fromMarkdown(MemoryBook& book, const QString& input)
{
    QString t = input;
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    QStringList lines = t.split(QLatin1Char('\n'));
    // YAML front matter: title, author, lang.
    if (!lines.isEmpty() && lines.first().trimmed() == QLatin1String("---")) {
        int end = -1;
        for (int i = 1; i < qMin<int>(int(lines.size()), 60); ++i)
            if (lines[i].trimmed() == QLatin1String("---") || lines[i].trimmed() == QLatin1String("...")) {
                end = i;
                break;
            }
        if (end > 0) {
            for (int i = 1; i < end; ++i) {
                const QString l = lines[i];
                const int colon = int(l.indexOf(QLatin1Char(':')));
                if (colon <= 0) continue;
                const QString key = l.left(colon).trimmed().toLower();
                QString value = l.mid(colon + 1).trimmed();
                if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\'')))) value = value.mid(1, value.size() - 2);
                if (key == QLatin1String("title") && book.metadata.title.isEmpty()) book.metadata.title = value;
                else if ((key == QLatin1String("author") || key == QLatin1String("creator")) && book.metadata.authors.isEmpty() && !value.isEmpty()) book.metadata.authors << value;
                else if ((key == QLatin1String("lang") || key == QLatin1String("language")) && book.metadata.language.isEmpty()) book.metadata.language = value;
            }
            lines = lines.mid(end + 1);
        }
    }
    if (book.metadata.language.isEmpty()) book.metadata.language = convert::guessLanguage(lines.join(QLatin1Char('\n')));
    const std::vector<MdBlock> blocks = mdBlocks(lines);
    // Chapters at the top heading level used more than once (# or ##).
    int count1 = 0, count2 = 0;
    for (const MdBlock& b : blocks) {
        if (b.kind == MdBlock::Heading && b.level == 1) ++count1;
        if (b.kind == MdBlock::Heading && b.level == 2) ++count2;
    }
    const int splitLevel = count1 >= 2 ? 1 : (count2 >= 2 ? 2 : (count1 == 1 ? 1 : 0));
    if (book.metadata.title.isEmpty() && count1 == 1)
        for (const MdBlock& b : blocks)
            if (b.kind == MdBlock::Heading && b.level == 1) book.metadata.title = QString(b.lines.value(0)).remove(QRegularExpression(QStringLiteral("[*_`]")));
    int part = 0;
    QString body, title;
    auto flush = [&](const QString& next) {
        if (!body.trimmed().isEmpty()) {
            ++part;
            const QString path = QStringLiteral("part%1.html").arg(part, 4, 10, QLatin1Char('0'));
            book.addChapter(path, convert::page(title.isEmpty() ? book.metadata.title : title, body, book.metadata.language), title);
        }
        body.clear();
        title = next;
    };
    for (const MdBlock& b : blocks) {
        if (b.kind == MdBlock::Heading && splitLevel && b.level <= splitLevel) {
            QString plain = b.lines.value(0);
            plain.remove(QRegularExpression(QStringLiteral("[*_`]")));
            flush(plain);
        } else if (b.kind == MdBlock::Heading && splitLevel && b.level == splitLevel + 1 && !body.isEmpty()) {
            // A sub-heading: an entry inside the chapter.
            const QString id = QStringLiteral("h%1").arg(book.toc.size() + 1);
            body += QStringLiteral("<h%1 id=\"%2\">%3</h%1>\n").arg(b.level).arg(id, markdownInline(b.lines.value(0)));
            book.addTocEntry(QString(b.lines.value(0)).remove(QRegularExpression(QStringLiteral("[*_`]"))),
                             QStringLiteral("part%1.html#%2").arg(part + 1, 4, 10, QLatin1Char('0')).arg(id), 1);
            continue;
        }
        body += mdBlockHtml(b);
    }
    flush(QString());
    if (book.spine.empty()) book.addChapter(QStringLiteral("part0001.html"), convert::page(book.metadata.title, QString(), book.metadata.language));
}

}  // namespace text

namespace {

void finish(MemoryBook& book, const QString& path, const QString& text)
{
    if (book.metadata.title.trimmed().isEmpty()) book.metadata.title = QFileInfo(path).completeBaseName();
    if (book.metadata.language.isEmpty()) book.metadata.language = convert::guessLanguage(text);
}

}  // namespace

bool TextFormat::canOpen(const QString& path, const QByteArray& head) const
{
    return hasSuffix(path, {"txt", "text"}) && looksLikeText(head);
}

std::shared_ptr<Book> TextFormat::open(const QString& path, QString* error) const
{
    const QByteArray data = readFile(path, error);
    if (data.isNull() && !QFileInfo(path).isReadable()) return nullptr;
    auto book = std::make_shared<MemoryBook>();
    book->filePath = path;
    const QString text = convert::decodeText(data);
    text::fromPlainText(*book, text);
    finish(*book, path, text);
    return book;
}

bool MarkdownFormat::canOpen(const QString& path, const QByteArray& head) const
{
    return hasSuffix(path, {"md", "markdown"}) && looksLikeText(head);
}

std::shared_ptr<Book> MarkdownFormat::open(const QString& path, QString* error) const
{
    const QByteArray data = readFile(path, error);
    if (data.isNull() && !QFileInfo(path).isReadable()) return nullptr;
    auto book = std::make_shared<MemoryBook>();
    book->filePath = path;
    book->diskRoot = QFileInfo(path).absolutePath();   // its pictures
    const QString text = convert::decodeText(data);
    text::fromMarkdown(*book, text);
    finish(*book, path, text);
    return book;
}

bool TxtzFormat::canOpen(const QString& path, const QByteArray& head) const
{
    return hasSuffix(path, {"txtz"}) && head.startsWith("PK\x03\x04");
}

std::shared_ptr<Book> TxtzFormat::open(const QString& path, QString* error) const
{
    auto book = std::make_shared<MemoryBook>();
    book->filePath = path;
    QString zipError;
    if (!book->zip.open(path, &zipError)) {
        if (error) *error = zipError;
        return nullptr;
    }
    book->zipOpen = true;
    // metadata.opf: title, authors, language.
    bool ok = false;
    const QByteArray opf = book->zip.read(QStringLiteral("metadata.opf"), &ok);
    if (ok) {
        const auto doc = dom::parse(opf, dom::ParseMode::Xml);
        std::function<void(const dom::Node*)> walk = [&](const dom::Node* n) {
            const QString text = n->textContent().simplified();
            if (n->is("dc:title") && book->metadata.title.isEmpty()) book->metadata.title = text;
            else if (n->is("dc:creator") && !text.isEmpty()) book->metadata.authors << text;
            else if (n->is("dc:language") && book->metadata.language.isEmpty()) book->metadata.language = text;
            else if (n->is("dc:description") && book->metadata.description.isEmpty()) book->metadata.description = text;
            for (const auto& c : n->children)
                if (c->isElement()) walk(c.get());
        };
        walk(doc->root.get());
    }
    QString textFile;
    bool markdown = false;
    for (const QString& n : book->zip.names()) {
        const QString s = QFileInfo(n).suffix().toLower();
        if (s == QLatin1String("txt") || s == QLatin1String("text") || s == QLatin1String("md") || s == QLatin1String("markdown") || s == QLatin1String("textile")) {
            textFile = n;
            markdown = s == QLatin1String("md") || s == QLatin1String("markdown");
            if (QFileInfo(n).completeBaseName().compare(QLatin1String("index"), Qt::CaseInsensitive) == 0) break;
        }
    }
    if (textFile.isEmpty()) {
        if (error) *error = QStringLiteral("The TXTZ file holds no text.");
        return nullptr;
    }
    const QString text = convert::decodeText(book->zip.read(textFile));
    // calibre writes Markdown into index.txt when it was made with the Markdown option: headings start with #.
    if (!markdown && QRegularExpression(QStringLiteral("(^|\\n)#{1,3} \\S")).match(text).hasMatch()) markdown = true;
    if (markdown) text::fromMarkdown(*book, text);
    else text::fromPlainText(*book, text);
    finish(*book, path, text);
    return book;
}

}  // namespace core

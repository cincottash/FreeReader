#include "OfficeFormats.h"

#include "core/format/Convert.h"
#include "core/format/html/HtmlFormat.h"
#include "core/format/text/TextFormats.h"
#include "core/html/HtmlParser.h"

#include <QSet>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>

#include <functional>

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

const dom::Node* child(const dom::Node* n, const char* name)
{
    return n ? n->firstElement(name) : nullptr;
}

// A Word or OpenDocument switch: present and not "0"/"false"/"none".
bool on(const dom::Node* n, const char* attr = "w:val")
{
    if (!n) return false;
    const QString v = n->attr(attr).toLower();
    return v.isEmpty() || (v != QLatin1String("0") && v != QLatin1String("false") && v != QLatin1String("none") && v != QLatin1String("off"));
}

struct Format {
    bool bold = false, italic = false, underline = false, strike = false, sup = false, sub = false;
    bool operator==(const Format& o) const
    {
        return bold == o.bold && italic == o.italic && underline == o.underline && strike == o.strike && sup == o.sup && sub == o.sub;
    }
};

QString wrap(const QString& text, const Format& f)
{
    if (text.isEmpty()) return text;
    QString s = text;
    if (f.sup) s = QStringLiteral("<sup>") + s + QStringLiteral("</sup>");
    if (f.sub) s = QStringLiteral("<sub>") + s + QStringLiteral("</sub>");
    if (f.strike) s = QStringLiteral("<del>") + s + QStringLiteral("</del>");
    if (f.underline) s = QStringLiteral("<u>") + s + QStringLiteral("</u>");
    if (f.italic) s = QStringLiteral("<em>") + s + QStringLiteral("</em>");
    if (f.bold) s = QStringLiteral("<strong>") + s + QStringLiteral("</strong>");
    return s;
}

// ---------------------------------------------------------------- DOCX

struct Docx {
    MemoryBook& book;
    QHash<QString, QString> rels;            // rId -> target (media/image1.png, or a URL)
    QHash<QString, QString> styleName;       // style id -> name ("heading 1")
    QHash<QString, int> styleLevel;          // style id -> heading level (1..6), from its outline level or name
    QHash<QString, QString> footnotes;       // id -> HTML
    QStringList usedNotes;
    convert::Chapters chapters;

    int levelOf(const QString& styleId)
    {
        if (styleLevel.contains(styleId)) return styleLevel.value(styleId);
        const QString name = styleName.value(styleId, styleId).toLower();
        static const QRegularExpression h(QStringLiteral("^(?:heading|título|titulo|überschrift|titre)\\s*(\\d)$"));
        if (const auto m = h.match(name); m.hasMatch()) return m.captured(1).toInt();
        if (name == QLatin1String("title") || name == QLatin1String("título") || name == QLatin1String("titulo")) return 1;
        return 0;
    }

    QString imageHref(const QString& rid)
    {
        const QString target = rels.value(rid);
        if (target.isEmpty()) return {};
        return target.startsWith(QLatin1Char('/')) ? target.mid(1) : QStringLiteral("word/") + target;
    }

    QString runs(const dom::Node* p, int* textLength)
    {
        QString out;
        std::function<void(const dom::Node*, const QString&)> walk = [&](const dom::Node* n, const QString& link) {
            for (const auto& cp : n->children) {
                const dom::Node* c = cp.get();
                if (!c->isElement()) continue;
                const QString& name = c->name;
                if (name == QLatin1String("w:r")) {
                    Format f;
                    if (const dom::Node* rpr = child(c, "w:rpr")) {
                        f.bold = rpr->firstElement("w:b") && on(rpr->firstElement("w:b"));
                        f.italic = rpr->firstElement("w:i") && on(rpr->firstElement("w:i"));
                        f.underline = rpr->firstElement("w:u") && on(rpr->firstElement("w:u"));
                        f.strike = rpr->firstElement("w:strike") && on(rpr->firstElement("w:strike"));
                        if (const dom::Node* va = rpr->firstElement("w:vertalign")) {
                            f.sup = va->attr("w:val") == QLatin1String("superscript");
                            f.sub = va->attr("w:val") == QLatin1String("subscript");
                        }
                    }
                    QString text;
                    for (const auto& r : c->children) {
                        if (!r->isElement()) continue;
                        if (r->is("w:t")) text += escape(r->textContent());
                        else if (r->is("w:tab")) text += QStringLiteral(" ");
                        else if (r->is("w:br") || r->is("w:cr")) text += r->attr("w:type") == QLatin1String("page") ? QString() : QStringLiteral("<br/>");
                        else if (r->is("w:nobreakhyphen")) text += QStringLiteral("-");
                        else if (r->is("w:sym")) text += QChar(r->attr("w:char").toUInt(nullptr, 16) & 0xFFFF);
                        else if (r->is("w:drawing") || r->is("w:pict") || r->is("w:object")) {
                            std::function<const dom::Node*(const dom::Node*)> blip = [&](const dom::Node* x) -> const dom::Node* {
                                if (x->is("a:blip") || x->is("v:imagedata")) return x;
                                for (const auto& y : x->children)
                                    if (y->isElement())
                                        if (const dom::Node* b = blip(y.get())) return b;
                                return nullptr;
                            };
                            if (const dom::Node* b = blip(r.get())) {
                                const QString href = imageHref(b->attr("r:embed").isEmpty() ? b->attr("r:id") : b->attr("r:embed"));
                                if (!href.isEmpty()) text += QStringLiteral("<img src=\"%1\" alt=\"\"/>").arg(escape(href));
                            }
                        } else if (r->is("w:footnotereference") || r->is("w:endnotereference")) {
                            const QString id = r->attr("w:id");
                            const QString key = (r->is("w:endnotereference") ? QStringLiteral("e") : QStringLiteral("f")) + id;
                            if (!usedNotes.contains(key)) usedNotes << key;
                            text += QStringLiteral("<sup><a href=\"#note-%1\" id=\"ref-%1\">%2</a></sup>").arg(key).arg(usedNotes.indexOf(key) + 1);
                        }
                    }
                    if (textLength) *textLength += int(text.size());
                    out += wrap(text, f);
                } else if (name == QLatin1String("w:hyperlink")) {
                    QString href = rels.value(c->attr("r:id"));
                    if (href.isEmpty() && !c->attr("w:anchor").isEmpty()) href = QLatin1Char('#') + c->attr("w:anchor");
                    out += QStringLiteral("<a href=\"%1\">").arg(escape(href));
                    walk(c, href);
                    out += QStringLiteral("</a>");
                } else if (name == QLatin1String("w:bookmarkstart")) {
                    if (!c->attr("w:name").isEmpty() && !c->attr("w:name").startsWith(QLatin1String("_GoBack")))
                        out += QStringLiteral("<a id=\"%1\"></a>").arg(escape(c->attr("w:name")));
                } else if (name == QLatin1String("w:del") || name == QLatin1String("w:movefrom") || name == QLatin1String("w:instrtext")) {
                    // deleted text and field codes aren't shown
                } else if (name == QLatin1String("w:ins") || name == QLatin1String("w:smarttag") || name == QLatin1String("w:sdt")
                           || name == QLatin1String("w:sdtcontent") || name == QLatin1String("w:fldsimple") || name == QLatin1String("w:customxml")
                           || name == QLatin1String("w:moveto")) {
                    walk(c, link);
                }
            }
        };
        walk(p, QString());
        return out;
    }

    bool pageBreakIn(const dom::Node* p)
    {
        std::function<bool(const dom::Node*)> f = [&](const dom::Node* n) {
            if (n->is("w:br") && n->attr("w:type") == QLatin1String("page")) return true;
            if (n->is("w:pagebreakbefore") && on(n)) return true;
            for (const auto& c : n->children)
                if (c->isElement() && f(c.get())) return true;
            return false;
        };
        return f(p);
    }

    void paragraph(const dom::Node* p)
    {
        const dom::Node* ppr = child(p, "w:ppr");
        const QString style = ppr && ppr->firstElement("w:pstyle") ? ppr->firstElement("w:pstyle")->attr("w:val") : QString();
        int level = levelOf(style);
        if (const dom::Node* ol = ppr ? ppr->firstElement("w:outlinelvl") : nullptr) {
            const int v = ol->attr("w:val").toInt();
            if (v >= 0 && v < 6) level = v + 1;
        }
        if (pageBreakIn(p)) chapters.pageBreak();
        int length = 0;
        const QString html = runs(p, &length);
        if (level > 0 && length > 0 && length < 300) {
            const auto doc = dom::parseText(QStringLiteral("<p>") + html + QStringLiteral("</p>"));
            chapters.heading(level, html, doc->root->textContent());
            return;
        }
        QString align;
        if (const dom::Node* jc = ppr ? ppr->firstElement("w:jc") : nullptr) {
            const QString v = jc->attr("w:val");
            if (v == QLatin1String("center")) align = QStringLiteral(" style=\"text-align:center\"");
            else if (v == QLatin1String("right") || v == QLatin1String("end")) align = QStringLiteral(" style=\"text-align:right\"");
        }
        const bool listItem = ppr && ppr->firstElement("w:numpr");
        if (html.trimmed().isEmpty()) return;
        chapters.block(QStringLiteral("<p%1>%2%3</p>\n").arg(align, listItem ? QStringLiteral("• ") : QString(), html), length);
    }

    void table(const dom::Node* t)
    {
        QString s = QStringLiteral("<table>");
        int length = 0;
        for (const dom::Node* tr : t->elements("w:tr")) {
            s += QStringLiteral("<tr>");
            for (const dom::Node* tc : tr->elements("w:tc")) {
                s += QStringLiteral("<td>");
                bool first = true;
                for (const dom::Node* p : tc->elements("w:p")) {
                    if (!first) s += QStringLiteral("<br/>");
                    s += runs(p, &length);
                    first = false;
                }
                s += QStringLiteral("</td>");
            }
            s += QStringLiteral("</tr>");
        }
        chapters.block(s + QStringLiteral("</table>\n"), length);
    }

    void body(const dom::Node* b)
    {
        for (const auto& cp : b->children) {
            const dom::Node* c = cp.get();
            if (!c->isElement()) continue;
            if (c->is("w:p")) paragraph(c);
            else if (c->is("w:tbl")) table(c);
            else if (c->is("w:sdt") || c->is("w:sdtcontent") || c->is("w:customxml")) body(c);
        }
    }
};

void readCoreProperties(MemoryBook& book, const QByteArray& xml)
{
    const auto doc = dom::parse(xml, dom::ParseMode::Xml);
    std::function<void(const dom::Node*)> walk = [&](const dom::Node* n) {
        const QString text = n->textContent().simplified();
        if (n->is("dc:title") && book.metadata.title.isEmpty()) book.metadata.title = text;
        else if ((n->is("dc:creator") || n->is("meta:initial-creator")) && !text.isEmpty() && book.metadata.authors.isEmpty()) book.metadata.authors << text;
        else if (n->is("dc:language") && book.metadata.language.isEmpty()) book.metadata.language = text;
        else if (n->is("dc:description") && book.metadata.description.isEmpty()) book.metadata.description = text;
        for (const auto& c : n->children)
            if (c->isElement()) walk(c.get());
    };
    walk(doc->root.get());
}

QString languageFromText(MemoryBook& book)
{
    QString sample;
    for (size_t i = 0; i < book.spine.size() && sample.size() < 40000; ++i) sample += dom::parse(book.resource(book.spine[i].path))->root->textContent();
    return convert::guessLanguage(sample);
}

// ---------------------------------------------------------------- ODT

struct Odt {
    MemoryBook& book;
    QHash<QString, Format> textStyles;      // automatic and named styles: their bold, italics…
    QHash<QString, int> paraLevel;          // paragraph style -> heading level (Title, Heading 1…)
    QHash<QString, QString> parentStyle;
    convert::Chapters chapters;
    int notes = 0;
    QString notesHtml;

    void readStyles(const dom::Node* n)
    {
        for (const auto& cp : n->children) {
            const dom::Node* c = cp.get();
            if (!c->isElement()) continue;
            if (c->is("style:style")) {
                const QString name = c->attr("style:name");
                Format f;
                if (const dom::Node* tp = c->firstElement("style:text-properties")) {
                    f.bold = tp->attr("fo:font-weight") == QLatin1String("bold") || tp->attr("fo:font-weight").toInt() >= 600;
                    f.italic = tp->attr("fo:font-style") == QLatin1String("italic");
                    f.underline = !tp->attr("style:text-underline-style").isEmpty() && tp->attr("style:text-underline-style") != QLatin1String("none");
                    f.strike = !tp->attr("style:text-line-through-style").isEmpty() && tp->attr("style:text-line-through-style") != QLatin1String("none");
                    const QString pos = tp->attr("style:text-position");
                    f.sup = pos.startsWith(QLatin1String("super")) || (pos.startsWith(QLatin1Char('3')));
                    f.sub = pos.startsWith(QLatin1String("sub")) || pos.startsWith(QLatin1Char('-'));
                }
                textStyles.insert(name, f);
                parentStyle.insert(name, c->attr("style:parent-style-name"));
                const QString display = c->attr("style:display-name").toLower();
                const QString lower = name.toLower();
                static const QRegularExpression h(QStringLiteral("^heading[ _]*(?:20_)?(\\d)$"));
                if (const auto m = h.match(lower); m.hasMatch()) paraLevel.insert(name, m.captured(1).toInt());
                else if (const auto d = h.match(display); d.hasMatch()) paraLevel.insert(name, d.captured(1).toInt());
                else if (lower == QLatin1String("title") || display == QLatin1String("title")) paraLevel.insert(name, 1);
                if (!c->attr("style:default-outline-level").isEmpty()) paraLevel.insert(name, c->attr("style:default-outline-level").toInt());
            }
            readStyles(c);
        }
    }

    int levelOf(const QString& style)
    {
        QString s = style;
        for (int guard = 0; guard < 8 && !s.isEmpty(); ++guard) {
            if (paraLevel.contains(s)) return paraLevel.value(s);
            s = parentStyle.value(s);
        }
        return 0;
    }

    QString inlineHtml(const dom::Node* n, int* length)
    {
        QString out;
        for (const auto& cp : n->children) {
            const dom::Node* c = cp.get();
            if (c->isText()) {
                out += escape(c->text);
                if (length) *length += int(c->text.size());
                continue;
            }
            const QString& name = c->name;
            if (name == QLatin1String("text:span")) out += wrap(inlineHtml(c, length), textStyles.value(c->attr("text:style-name")));
            else if (name == QLatin1String("text:a")) out += QStringLiteral("<a href=\"%1\">%2</a>").arg(escape(c->attr("xlink:href")), inlineHtml(c, length));
            else if (name == QLatin1String("text:s")) out += QString(qMax(1, c->attr("text:c").toInt()), QLatin1Char(' '));
            else if (name == QLatin1String("text:tab")) out += QLatin1Char(' ');
            else if (name == QLatin1String("text:line-break")) out += QStringLiteral("<br/>");
            else if (name == QLatin1String("text:bookmark") || name == QLatin1String("text:bookmark-start")) out += QStringLiteral("<a id=\"%1\"></a>").arg(escape(c->attr("text:name")));
            else if (name == QLatin1String("text:note")) {
                ++notes;
                const QString id = QStringLiteral("note-%1").arg(notes);
                out += QStringLiteral("<sup><a href=\"#%1\" id=\"ref-%2\">%2</a></sup>").arg(id).arg(notes);
                QString body;
                if (const dom::Node* nb = c->firstElement("text:note-body"))
                    for (const auto& p : nb->children)
                        if (p->isElement()) body += inlineHtml(p.get(), nullptr) + QLatin1Char(' ');
                notesHtml += QStringLiteral("<p id=\"%1\"><a href=\"#ref-%2\">%2.</a> %3</p>\n").arg(id).arg(notes).arg(body.trimmed());
            } else if (name == QLatin1String("draw:frame")) {
                if (const dom::Node* img = c->firstElement("draw:image")) {
                    const QString href = img->attr("xlink:href");
                    if (!href.isEmpty() && !href.startsWith(QLatin1String("http"))) out += QStringLiteral("<img src=\"%1\" alt=\"\"/>").arg(escape(href));
                }
            } else if (name == QLatin1String("text:soft-page-break") || name == QLatin1String("office:annotation")) {
            } else {
                out += inlineHtml(c, length);
            }
        }
        return out;
    }

    void blocks(const dom::Node* n)
    {
        for (const auto& cp : n->children) {
            const dom::Node* c = cp.get();
            if (!c->isElement()) continue;
            const QString& name = c->name;
            if (name == QLatin1String("text:h") || name == QLatin1String("text:p")) {
                int length = 0;
                const QString html = inlineHtml(c, &length);
                int level = name == QLatin1String("text:h") ? qMax(1, c->attr("text:outline-level").toInt()) : levelOf(c->attr("text:style-name"));
                if (level > 0 && length > 0 && length < 300) {
                    chapters.heading(qMin(level, 6), html, dom::parseText(QStringLiteral("<p>") + html + QStringLiteral("</p>"))->root->textContent());
                } else if (!html.trimmed().isEmpty()) {
                    chapters.block(wrap(QStringLiteral("<p>%1</p>\n").arg(html), Format()), length);
                }
            } else if (name == QLatin1String("text:list")) {
                for (const dom::Node* item : c->elements("text:list-item"))
                    for (const auto& p : item->children) {
                        if (!p->isElement()) continue;
                        if (p->is("text:list")) {
                            blocks(item);
                            break;
                        }
                        int length = 0;
                        const QString html = inlineHtml(p.get(), &length);
                        if (!html.trimmed().isEmpty()) chapters.block(QStringLiteral("<p>• %1</p>\n").arg(html), length);
                    }
            } else if (name == QLatin1String("table:table")) {
                QString s = QStringLiteral("<table>");
                std::function<void(const dom::Node*)> rows = [&](const dom::Node* x) {
                    for (const auto& r : x->children) {
                        if (!r->isElement()) continue;
                        if (r->is("table:table-row")) {
                            s += QStringLiteral("<tr>");
                            for (const dom::Node* cell : r->elements("table:table-cell")) {
                                s += QStringLiteral("<td>");
                                for (const auto& p : cell->children)
                                    if (p->isElement()) s += inlineHtml(p.get(), nullptr) + QLatin1Char(' ');
                                s += QStringLiteral("</td>");
                            }
                            s += QStringLiteral("</tr>");
                        } else {
                            rows(r.get());   // table:table-rows, table:table-header-rows
                        }
                    }
                };
                rows(c);
                chapters.block(s + QStringLiteral("</table>\n"));
            } else if (name == QLatin1String("text:section") || name == QLatin1String("text:index-body") || name == QLatin1String("text:table-of-content")) {
                if (name == QLatin1String("text:section")) blocks(c);
            }
        }
    }
};

// ---------------------------------------------------------------- RTF

class Rtf {
public:
    explicit Rtf(MemoryBook& b) : book(b) {}

    void parse(const QByteArray& data)
    {
        d = data;
        State st;
        std::vector<State> stack;
        qsizetype i = 0;
        const qsizetype n = d.size();
        while (i < n) {
            const char c = d[i];
            if (c == '{') {
                stack.push_back(st);
                st.newGroup = true;
                ++i;
                continue;
            }
            if (c == '}') {
                endGroup(st);
                if (!stack.empty()) {
                    const bool wasPict = st.dest == Dest::Pict;
                    if (wasPict) addPicture(st);
                    st = stack.back();
                    stack.pop_back();
                }
                ++i;
                continue;
            }
            if (c == '\\') {
                ++i;
                if (i >= n) break;
                const char k = d[i];
                if (k == '\'' && i + 2 < n) {
                    const int v = QByteArray(d.constData() + i + 1, 2).toInt(nullptr, 16);
                    i += 3;
                    if (st.skip > 0) {
                        --st.skip;
                        continue;
                    }
                    emitByte(st, char(v));
                    continue;
                }
                if (k == '\\' || k == '{' || k == '}') {
                    if (st.skip > 0) --st.skip;
                    else emitText(st, QString(QChar(k)));
                    ++i;
                    continue;
                }
                if (k == '~') {
                    emitText(st, QString(QChar(0x00A0)));
                    ++i;
                    continue;
                }
                if (k == '_') {
                    emitText(st, QStringLiteral("-"));
                    ++i;
                    continue;
                }
                if (k == '-') {
                    ++i;
                    continue;
                }
                if (k == '*') {
                    st.ignorable = true;
                    ++i;
                    continue;
                }
                if (k == '\n' || k == '\r') {
                    control(st, "par", 0, false);
                    ++i;
                    continue;
                }
                qsizetype j = i;
                while (j < n && ((d[j] >= 'a' && d[j] <= 'z') || (d[j] >= 'A' && d[j] <= 'Z'))) ++j;
                const QByteArray word = d.mid(i, j - i);
                bool hasParam = false;
                int param = 0;
                qsizetype k2 = j;
                if (k2 < n && (d[k2] == '-' || (d[k2] >= '0' && d[k2] <= '9'))) {
                    qsizetype e = k2 + 1;
                    while (e < n && d[e] >= '0' && d[e] <= '9') ++e;
                    param = d.mid(k2, e - k2).toInt();
                    hasParam = true;
                    k2 = e;
                }
                if (k2 < n && d[k2] == ' ') ++k2;
                i = k2;
                if (word.isEmpty()) continue;
                if (st.dest == Dest::Pict && st.skip == 0 && word != "u") {
                    pictControl(st, word, param);
                }
                control(st, word, param, hasParam);
                continue;
            }
            if (c == '\r' || c == '\n') {
                ++i;
                continue;
            }
            // Plain text (and the hex of a picture).
            qsizetype j = i;
            while (j < n && d[j] != '\\' && d[j] != '{' && d[j] != '}' && d[j] != '\r' && d[j] != '\n') ++j;
            const QByteArray run = d.mid(i, j - i);
            i = j;
            if (st.dest == Dest::Pict) {
                st.pictHex += run;
                continue;
            }
            for (const char b : run) {
                if (st.skip > 0) {
                    --st.skip;
                    continue;
                }
                emitByte(st, b);
            }
        }
        endParagraph(st);
    }

    convert::Chapters chapters;

private:
    enum class Dest { Text, Skip, FontTable, StyleSheet, Info, Title, Author, Pict, FieldInstruction };
    struct State {
        Dest dest = Dest::Text;
        bool ignorable = false, newGroup = false;
        Format f;
        int uc = 1, skip = 0, font = -1, style = 0;
        int align = 0;   // 0 left, 1 center, 2 right
        int outline = -1;
        QByteArray pictHex;
        QString pictType;
        int fontIndex = -1;   // in the font table: the font being described
        QString styleText;
    };

    MemoryBook& book;
    QByteArray d;
    QString para;            // HTML of the paragraph being read
    int paraLength = 0;
    Format current;          // formatting of the run being written
    QString run;
    QHash<int, int> fontCharset;
    QHash<int, QString> styleNames;
    int codepage = 1252;
    QByteArray pendingBytes;   // multi-byte characters (\'hh\'hh) in a code page
    int pictures = 0;
    QString info;

    QString codepageName(int cp) const
    {
        switch (cp) {
        case 1251: return QStringLiteral("windows-1251");
        case 65001: return QStringLiteral("utf-8");
        case 1250: return QStringLiteral("windows-1250");
        case 1253: return QStringLiteral("windows-1253");
        case 1254: return QStringLiteral("windows-1254");
        case 932: return QStringLiteral("Shift_JIS");
        case 936: return QStringLiteral("GBK");
        case 949: return QStringLiteral("EUC-KR");
        case 950: return QStringLiteral("Big5");
        default: return QStringLiteral("windows-1252");
        }
    }
    int fontCodepage(const State& st) const
    {
        if (st.font >= 0 && fontCharset.contains(st.font)) {
            switch (fontCharset.value(st.font)) {
            case 204: return 1251;
            case 238: return 1250;
            case 161: return 1253;
            case 162: return 1254;
            case 128: return 932;
            case 134: return 936;
            case 129: return 949;
            case 136: return 950;
            case 2: return -1;   // Symbol
            default: break;
            }
        }
        return codepage;
    }

    void emitByte(State& st, char b)
    {
        const int cp = fontCodepage(st);
        if (cp == -1) return;   // symbol fonts: no letters
        const QString enc = codepageName(cp);
        if (cp == 932 || cp == 936 || cp == 949 || cp == 950) {
            pendingBytes += b;
            const QString s = convert::decodeAs(pendingBytes, enc);
            if (!s.contains(QChar::ReplacementCharacter)) {
                emitText(st, s);
                pendingBytes.clear();
            } else if (pendingBytes.size() >= 2) {
                pendingBytes.clear();
            }
            return;
        }
        emitText(st, convert::decodeAs(QByteArray(1, b), enc));
    }

    void emitText(State& st, const QString& text)
    {
        switch (st.dest) {
        case Dest::Text:
            break;
        case Dest::Title:
            if (book.metadata.title.size() < 300) book.metadata.title += text;
            return;
        case Dest::Author:
            info += text;
            return;
        case Dest::FontTable:
            return;
        case Dest::StyleSheet:
            st.styleText += text;
            return;
        default:
            return;
        }
        if (!(st.f == current)) flushRun();
        current = st.f;
        run += text;
        paraLength += int(text.size());
        lastAlign = st.align;
        lastOutline = st.outline;
        lastStyle = st.style;
    }

    int lastAlign = 0, lastOutline = -1, lastStyle = 0;

    void flushRun()
    {
        if (run.isEmpty()) return;
        para += wrap(escape(run), current);
        run.clear();
    }

    void endParagraph(const State& st)
    {
        Q_UNUSED(st);
        flushRun();
        const QString html = para;
        para.clear();
        const int length = paraLength;
        paraLength = 0;
        if (html.trimmed().isEmpty() || html == QLatin1String("<br/>")) return;
        int level = lastOutline >= 0 && lastOutline < 6 ? lastOutline + 1 : 0;
        if (!level) {
            static const QRegularExpression h(QStringLiteral("^(?:heading|título|titulo)\\s*(\\d)$"), QRegularExpression::CaseInsensitiveOption);
            if (const auto m = h.match(styleNames.value(lastStyle)); m.hasMatch()) level = m.captured(1).toInt();
            else if (styleNames.value(lastStyle).compare(QLatin1String("title"), Qt::CaseInsensitive) == 0) level = 1;
        }
        if (level > 0 && length < 300) {
            chapters.heading(level, html, dom::parseText(QStringLiteral("<p>") + html + QStringLiteral("</p>"))->root->textContent());
            return;
        }
        const QString align = lastAlign == 1 ? QStringLiteral(" style=\"text-align:center\"") : lastAlign == 2 ? QStringLiteral(" style=\"text-align:right\"") : QString();
        chapters.block(QStringLiteral("<p%1>%2</p>\n").arg(align, html), length);
    }

    void endGroup(State& st)
    {
        if (st.dest == Dest::StyleSheet && st.style > 0 && !st.styleText.isEmpty()) {
            QString name = st.styleText.trimmed();
            if (name.endsWith(QLatin1Char(';'))) name.chop(1);
            styleNames.insert(st.style, name.trimmed());
        }
        if (st.dest == Dest::Author && !info.trimmed().isEmpty()) {
            if (book.metadata.authors.isEmpty()) book.metadata.authors << info.trimmed();
            info.clear();
        }
    }

    void pictControl(State& st, const QByteArray& w, int)
    {
        if (w == "pngblip") st.pictType = QStringLiteral("png");
        else if (w == "jpegblip") st.pictType = QStringLiteral("jpg");
    }

    void addPicture(State& st)
    {
        if (st.pictType.isEmpty() || st.pictHex.isEmpty()) return;
        const QByteArray bytes = QByteArray::fromHex(st.pictHex);
        if (convert::imageType(bytes).isEmpty()) return;
        const QString path = QStringLiteral("images/pict%1.%2").arg(++pictures, 3, 10, QLatin1Char('0')).arg(st.pictType);
        book.addFile(path, bytes);
        flushRun();
        para += QStringLiteral("<img src=\"%1\" alt=\"\"/>").arg(path);
    }

    void control(State& st, const QByteArray& w, int param, bool hasParam)
    {
        const bool first = st.newGroup;
        st.newGroup = false;
        // Destinations: groups whose text isn't the document's.
        if (first || st.ignorable) {
            static const QSet<QByteArray> skip = {"colortbl", "header", "headerl", "headerr", "headerf", "footer", "footerl", "footerr", "footerf",
                                                  "object", "themedata", "colorschememapping", "latentstyles", "datastore", "xmlnstbl", "listtable",
                                                  "listoverridetable", "rsidtbl", "generator", "mmathPr", "pgdsctbl", "fldinst", "bkmkstart", "bkmkend",
                                                  "comment", "annotation", "atnid", "atnauthor", "operator", "company", "doccomm", "keywords", "subject",
                                                  "category", "manager", "hlinkbase", "nonshppict", "pnseclvl", "pntext", "pntxtb", "pntxta", "listtext",
                                                  "revtbl", "protusertbl", "wgrffmtfilter", "passwordhash", "docvar", "userprops", "ftnsep", "ftnsepc",
                                                  "aftnsep", "aftnsepc", "footnote", "xe", "tc", "sp", "sn", "sv", "fonttbl_alt", "falt", "panose",
                                                  "creatim", "revtim", "printim", "buptim", "version", "vern", "edmins", "nofpages", "nofwords", "nofchars",
                                                  "id", "background", "blipuid"};
            if (w == "fonttbl") {
                st.dest = Dest::FontTable;
                return;
            }
            if (w == "stylesheet") {
                st.dest = Dest::StyleSheet;
                return;
            }
            if (w == "info") {
                st.dest = Dest::Info;
                return;
            }
            if (w == "title" && st.dest == Dest::Info) {
                st.dest = Dest::Title;
                return;
            }
            if (w == "author" && st.dest == Dest::Info) {
                st.dest = Dest::Author;
                return;
            }
            if (w == "pict") {
                st.dest = Dest::Pict;
                return;
            }
            // {\*\shppict{\pict…}}: the picture inside is read; other unknown {\*…} groups are skipped.
            if (w == "shppict" || w == "ud" || w == "upr") return;
            if (skip.contains(w) || st.ignorable) {
                st.dest = Dest::Skip;
                return;
            }
        }
        if (st.dest == Dest::Skip || st.dest == Dest::Info || st.dest == Dest::Pict) return;
        if (w == "ansicpg") codepage = param;
        else if (w == "mac") codepage = 10000;
        else if (w == "f") {
            if (st.dest == Dest::FontTable) st.fontIndex = param;
            else st.font = param;
        } else if (w == "fcharset" && st.dest == Dest::FontTable) fontCharset.insert(st.fontIndex, param);
        else if (w == "s" || w == "cs" || w == "ds") {
            st.style = param;
        } else if (w == "par" || w == "sect") {
            if (st.dest == Dest::Text) endParagraph(st);
            if (w == "sect") chapters.pageBreak();
        } else if (w == "page") {
            if (st.dest == Dest::Text) {
                endParagraph(st);
                chapters.pageBreak();
            }
        } else if (w == "line") {
            if (st.dest == Dest::Text) {
                flushRun();
                para += QStringLiteral("<br/>");
            }
        } else if (w == "tab") {
            emitText(st, QStringLiteral(" "));
        } else if (w == "cell" || w == "row") {
            emitText(st, QStringLiteral(" "));
            if (w == "row" && st.dest == Dest::Text) endParagraph(st);
        } else if (w == "emdash") emitText(st, QString(QChar(0x2014)));
        else if (w == "endash") emitText(st, QString(QChar(0x2013)));
        else if (w == "bullet") emitText(st, QString(QChar(0x2022)));
        else if (w == "lquote") emitText(st, QString(QChar(0x2018)));
        else if (w == "rquote") emitText(st, QString(QChar(0x2019)));
        else if (w == "ldblquote") emitText(st, QString(QChar(0x201C)));
        else if (w == "rdblquote") emitText(st, QString(QChar(0x201D)));
        else if (w == "u") {
            int v = param;
            if (v < 0) v += 65536;
            emitText(st, QString(QChar(char16_t(v))));
            st.skip = st.uc;
        } else if (w == "uc") st.uc = param;
        else if (w == "b") st.f.bold = !hasParam || param != 0;
        else if (w == "i") st.f.italic = !hasParam || param != 0;
        else if (w == "ul") st.f.underline = !hasParam || param != 0;
        else if (w == "ulnone") st.f.underline = false;
        else if (w == "strike") st.f.strike = !hasParam || param != 0;
        else if (w == "super") st.f.sup = true;
        else if (w == "sub") st.f.sub = true;
        else if (w == "nosupersub") st.f.sup = st.f.sub = false;
        else if (w == "plain") {
            st.f = Format();
        } else if (w == "pard") {
            st.align = 0;
            st.outline = -1;
            st.style = 0;
        } else if (w == "qc") st.align = 1;
        else if (w == "qr") st.align = 2;
        else if (w == "ql" || w == "qj") st.align = 0;
        else if (w == "outlinelevel") st.outline = param;
    }
};

// ---------------------------------------------------------------- DOC (Word 97-2003)

quint32 le32(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 4 > d.size()) return 0;
    const auto* p = reinterpret_cast<const uchar*>(d.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}
quint16 le16(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 2 > d.size()) return 0;
    const auto* p = reinterpret_cast<const uchar*>(d.constData() + at);
    return quint16(p[0] | (p[1] << 8));
}

// A compound file (OLE2): its streams by name.
class Cfb {
public:
    bool open(const QByteArray& data)
    {
        d = data;
        if (!d.startsWith("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1")) return false;
        shift = le16(d, 0x1E);
        miniShift = le16(d, 0x20);
        if (shift < 7 || shift > 16 || miniShift > shift) return false;
        const quint32 firstDir = le32(d, 0x30), cutoffValue = le32(d, 0x38), firstMiniFat = le32(d, 0x3C), firstDifat = le32(d, 0x44);
        cutoff = cutoffValue;
        // The FAT: its sectors are listed in the header (109) and then in DIFAT sectors.
        std::vector<quint32> fatSectors;
        for (int i = 0; i < 109; ++i) {
            const quint32 s = le32(d, 0x4C + 4 * i);
            if (s < 0xFFFFFFFA) fatSectors.push_back(s);
        }
        quint32 difat = firstDifat;
        for (int guard = 0; difat < 0xFFFFFFFA && guard < 10000; ++guard) {
            const qsizetype off = sectorOffset(difat);
            const int per = (1 << shift) / 4;
            for (int i = 0; i < per - 1; ++i) {
                const quint32 s = le32(d, off + 4 * i);
                if (s < 0xFFFFFFFA) fatSectors.push_back(s);
            }
            difat = le32(d, off + 4 * (per - 1));
        }
        for (const quint32 s : fatSectors) {
            const qsizetype off = sectorOffset(s);
            for (int i = 0; i < (1 << shift) / 4; ++i) fat.push_back(le32(d, off + 4 * i));
        }
        const QByteArray dir = chain(firstDir);
        for (qsizetype p = 0; p + 128 <= dir.size(); p += 128) {
            const int nameLen = le16(dir, p + 64);
            Entry e;
            e.name = QString::fromUtf16(reinterpret_cast<const char16_t*>(dir.constData() + p), qMax(0, nameLen / 2 - 1));
            e.type = uchar(dir.at(p + 66));
            e.start = le32(dir, p + 116);
            e.size = le32(dir, p + 120);
            entries.push_back(e);
        }
        if (entries.empty()) return false;
        miniStream = chain(entries[0].start).left(entries[0].size);
        const QByteArray miniFatData = chain(firstMiniFat);
        for (qsizetype p = 0; p + 4 <= miniFatData.size(); p += 4) miniFat.push_back(le32(miniFatData, p));
        return true;
    }

    QByteArray stream(const QString& name, bool* ok = nullptr) const
    {
        for (const Entry& e : entries)
            if (e.type == 2 && e.name == name) {
                if (ok) *ok = true;
                if (e.size < cutoff) {
                    QByteArray out;
                    quint32 s = e.start;
                    const int size = 1 << miniShift;
                    for (int guard = 0; s < 0xFFFFFFFA && s < miniFat.size() + 1 && guard < 1000000 && quint32(out.size()) < e.size; ++guard) {
                        out += miniStream.mid(qsizetype(s) * size, size);
                        if (s >= miniFat.size()) break;
                        s = miniFat[s];
                    }
                    return out.left(e.size);
                }
                return chain(e.start).left(e.size);
            }
        if (ok) *ok = false;
        return {};
    }

private:
    struct Entry {
        QString name;
        int type = 0;
        quint32 start = 0, size = 0;
    };
    QByteArray d, miniStream;
    int shift = 9, miniShift = 6;
    quint32 cutoff = 4096;
    std::vector<quint32> fat, miniFat;
    std::vector<Entry> entries;

    qsizetype sectorOffset(quint32 s) const { return (qsizetype(s) + 1) << shift; }
    QByteArray chain(quint32 s) const
    {
        QByteArray out;
        for (int guard = 0; s < 0xFFFFFFFA && guard < 1000000; ++guard) {
            out += d.mid(sectorOffset(s), 1 << shift);
            if (s >= fat.size()) break;
            s = fat[s];
        }
        return out;
    }
};

// The main text of a Word 97-2003 document, or an error.
QString wordText(const QByteArray& file, QString* error)
{
    Cfb cfb;
    if (!cfb.open(file)) {
        if (error) *error = QStringLiteral("This .doc file is from a Word version older than Word 97, or damaged. Saving it as DOCX in a word processor opens it here.");
        return {};
    }
    bool ok = false;
    const QByteArray wd = cfb.stream(QStringLiteral("WordDocument"), &ok);
    if (!ok || le16(wd, 0) != 0xA5EC) {
        if (error) *error = QStringLiteral("This file isn't a Word document.");
        return {};
    }
    const quint16 flags = le16(wd, 0x0A);
    if (flags & 0x0100) {
        if (error) *error = QStringLiteral("This Word document is locked with a password, so it can't be opened.");
        return {};
    }
    const QByteArray table = cfb.stream((flags & 0x0200) ? QStringLiteral("1Table") : QStringLiteral("0Table"), &ok);
    const quint32 ccpText = le32(wd, 0x4C);
    const quint32 fcClx = le32(wd, 0x01A2), lcbClx = le32(wd, 0x01A6);
    QString text;
    if (ok && lcbClx > 0 && fcClx + lcbClx <= quint32(table.size())) {
        // The piece table: where each run of text is, and whether it is 8-bit or UTF-16.
        qsizetype p = fcClx;
        const qsizetype end = qsizetype(fcClx) + lcbClx;
        while (p < end && table.at(p) == 0x01) p += 3 + le16(table, p + 1);
        if (p < end && table.at(p) == 0x02) {
            const quint32 lcb = le32(table, p + 1);
            const qsizetype plc = p + 5;
            const int n = int((lcb - 4) / 12);
            for (int i = 0; i < n; ++i) {
                const quint32 cpStart = le32(table, plc + 4 * i), cpEnd = le32(table, plc + 4 * (i + 1));
                if (cpStart >= ccpText) break;
                const quint32 count = qMin(cpEnd, ccpText) - cpStart;
                const qsizetype pcd = plc + 4 * (n + 1) + 8 * i;
                quint32 fc = le32(table, pcd + 2);
                const bool compressed = fc & 0x40000000;
                fc &= ~0x40000000u;
                if (compressed) text += convert::decodeAs(wd.mid(fc / 2, count), QStringLiteral("windows-1252"));
                else text += QString::fromUtf16(reinterpret_cast<const char16_t*>(wd.constData() + fc), qMin<qsizetype>(count, (wd.size() - fc) / 2));
            }
        }
    }
    if (text.isEmpty()) {
        // No piece table: the text sits in one place (fcMin..fcMac).
        const quint32 fcMin = le32(wd, 0x18), fcMac = le32(wd, 0x1C);
        if (fcMac > fcMin && fcMac <= quint32(wd.size())) text = convert::decodeAs(wd.mid(fcMin, fcMac - fcMin), QStringLiteral("windows-1252"));
    }
    // Fields: the code (between 0x13 and 0x14) isn't shown, the result (up to 0x15) is.
    QString out;
    int depth = 0;
    bool code = false;
    for (const QChar c : text) {
        const char16_t u = c.unicode();
        if (u == 0x13) {
            ++depth;
            code = true;
            continue;
        }
        if (u == 0x14) {
            code = false;
            continue;
        }
        if (u == 0x15) {
            depth = qMax(0, depth - 1);
            code = false;
            continue;
        }
        if (code && depth) continue;
        if (u == 0x0D || u == 0x07) out += QLatin1Char('\n');
        else if (u == 0x0B) out += QChar(0x2028);
        else if (u == 0x0C) out += QStringLiteral("\n\f\n");
        else if (u == 0x1E) out += QLatin1Char('-');
        else if (u == 0x1F || u < 0x09 || u == 0xFFFF) continue;
        else out += c;
    }
    return out;
}

QByteArray readAll(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The file can't be read: %1").arg(f.errorString());
        return {};
    }
    return f.readAll();
}

}  // namespace

// ---------------------------------------------------------------- plug-ins

bool DocxFormat::canOpen(const QString& path, const QByteArray& head) const
{
    return hasSuffix(path, {"docx", "docm", "dotx"}) && head.startsWith("PK\x03\x04");
}

std::shared_ptr<Book> DocxFormat::open(const QString& path, QString* error) const
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
    const QByteArray document = book->zip.read(QStringLiteral("word/document.xml"), &ok);
    if (!ok) {
        if (error)
            *error = book->zip.contains(QStringLiteral("EncryptedPackage")) ? QStringLiteral("This Word document is locked with a password, so it can't be opened.")
                                                                           : QStringLiteral("The document is damaged: it has no text part.");
        return nullptr;
    }
    Docx dx{*book, {}, {}, {}, {}, {}, {}};
    if (const QByteArray rels = book->zip.read(QStringLiteral("word/_rels/document.xml.rels"), &ok); ok) {
        const auto doc = dom::parse(rels, dom::ParseMode::Xml);
        std::function<void(const dom::Node*)> walk = [&](const dom::Node* n) {
            if (n->is("relationship")) dx.rels.insert(n->id().isEmpty() ? n->attr("Id") : n->id(), n->attr("target").isEmpty() ? n->attr("Target") : n->attr("target"));
            for (const auto& c : n->children)
                if (c->isElement()) walk(c.get());
        };
        walk(doc->root.get());
    }
    if (const QByteArray styles = book->zip.read(QStringLiteral("word/styles.xml"), &ok); ok) {
        const auto doc = dom::parse(styles, dom::ParseMode::Xml);
        std::function<void(const dom::Node*)> walk = [&](const dom::Node* n) {
            if (n->is("w:style")) {
                const QString id = n->attr("w:styleid");
                if (const dom::Node* name = n->firstElement("w:name")) dx.styleName.insert(id, name->attr("w:val"));
                if (const dom::Node* ppr = n->firstElement("w:ppr"))
                    if (const dom::Node* ol = ppr->firstElement("w:outlinelvl")) {
                        const int v = ol->attr("w:val").toInt();
                        if (v >= 0 && v < 6) dx.styleLevel.insert(id, v + 1);
                    }
            }
            for (const auto& c : n->children)
                if (c->isElement()) walk(c.get());
        };
        walk(doc->root.get());
    }
    if (const QByteArray props = book->zip.read(QStringLiteral("docProps/core.xml"), &ok); ok) readCoreProperties(*book, props);
    // Footnotes and endnotes, for a Notes chapter at the end.
    for (const auto& [file, prefix] : {std::pair<const char*, const char*>{"word/footnotes.xml", "f"}, {"word/endnotes.xml", "e"}}) {
        const QByteArray notes = book->zip.read(QString::fromLatin1(file), &ok);
        if (!ok) continue;
        const auto doc = dom::parse(notes, dom::ParseMode::Xml);
        std::function<void(const dom::Node*)> walk = [&](const dom::Node* n) {
            if (n->is("w:footnote") || n->is("w:endnote")) {
                QString html;
                for (const dom::Node* p : n->elements("w:p")) html += dx.runs(p, nullptr) + QLatin1Char(' ');
                dx.footnotes.insert(QString::fromLatin1(prefix) + n->attr("w:id"), html.trimmed());
                return;
            }
            for (const auto& c : n->children)
                if (c->isElement()) walk(c.get());
        };
        walk(doc->root.get());
    }
    const auto doc = dom::parse(document, dom::ParseMode::Xml);
    const dom::Node* root = doc->documentElement();
    if (const dom::Node* body = root ? root->firstElement("w:body") : nullptr) dx.body(body);
    if (!dx.usedNotes.isEmpty()) {
        dx.chapters.heading(1, QStringLiteral("Notes"), QStringLiteral("Notes"), QStringLiteral("notes"));
        for (int i = 0; i < dx.usedNotes.size(); ++i) {
            const QString key = dx.usedNotes[i];
            dx.chapters.block(QStringLiteral("<p id=\"note-%1\"><a href=\"#ref-%1\">%2.</a> %3</p>\n").arg(key).arg(i + 1).arg(dx.footnotes.value(key)));
        }
    }
    if (book->metadata.title.trimmed().isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();
    if (dx.chapters.isEmpty()) {
        if (error) *error = QStringLiteral("The document has no text.");
        return nullptr;
    }
    dx.chapters.finish(*book);
    if (book->metadata.language.isEmpty() || book->metadata.language.size() > 12) book->metadata.language = languageFromText(*book);
    return book;
}

bool OdtFormat::canOpen(const QString& path, const QByteArray& head) const
{
    if (hasSuffix(path, {"fodt"})) return true;
    return hasSuffix(path, {"odt", "ott"}) && head.startsWith("PK\x03\x04");
}

std::shared_ptr<Book> OdtFormat::open(const QString& path, QString* error) const
{
    auto book = std::make_shared<MemoryBook>();
    book->filePath = path;
    QByteArray content, styles, meta;
    if (hasSuffix(path, {"fodt"})) {
        content = readAll(path, error);   // flat ODT: one XML with everything
        styles = content;
        meta = content;
    } else {
        QString zipError;
        if (!book->zip.open(path, &zipError)) {
            if (error) *error = zipError;
            return nullptr;
        }
        book->zipOpen = true;
        bool ok = false;
        content = book->zip.read(QStringLiteral("content.xml"), &ok);
        if (!ok) {
            if (error) *error = QStringLiteral("The document is damaged: it has no content part.");
            return nullptr;
        }
        const QByteArray manifest = book->zip.read(QStringLiteral("META-INF/manifest.xml"));
        if (manifest.contains("encryption-data")) {
            if (error) *error = QStringLiteral("This document is locked with a password, so it can't be opened.");
            return nullptr;
        }
        styles = book->zip.read(QStringLiteral("styles.xml"));
        meta = book->zip.read(QStringLiteral("meta.xml"));
    }
    if (!meta.isEmpty()) readCoreProperties(*book, meta);
    Odt od{*book, {}, {}, {}, {}, 0, {}};
    if (!styles.isEmpty()) od.readStyles(dom::parse(styles, dom::ParseMode::Xml)->root.get());
    const auto doc = dom::parse(content, dom::ParseMode::Xml);
    od.readStyles(doc->root.get());
    std::function<const dom::Node*(const dom::Node*)> findText = [&](const dom::Node* n) -> const dom::Node* {
        if (n->is("office:text")) return n;
        for (const auto& c : n->children)
            if (c->isElement())
                if (const dom::Node* f = findText(c.get())) return f;
        return nullptr;
    };
    if (const dom::Node* text = findText(doc->root.get())) od.blocks(text);
    if (!od.notesHtml.isEmpty()) {
        od.chapters.heading(1, QStringLiteral("Notes"), QStringLiteral("Notes"), QStringLiteral("notes"));
        od.chapters.block(od.notesHtml);
    }
    if (od.chapters.isEmpty()) {
        if (error) *error = QStringLiteral("The document has no text.");
        return nullptr;
    }
    if (book->metadata.title.trimmed().isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();
    od.chapters.finish(*book);
    if (book->metadata.language.isEmpty()) book->metadata.language = languageFromText(*book);
    return book;
}

bool RtfFormat::canOpen(const QString& path, const QByteArray& head) const
{
    return head.startsWith("{\\rtf") || (hasSuffix(path, {"rtf"}) && !head.contains('\0'));
}

std::shared_ptr<Book> RtfFormat::open(const QString& path, QString* error) const
{
    const QByteArray data = readAll(path, error);
    if (!data.startsWith("{\\rtf")) {
        if (error) *error = QStringLiteral("The file isn't RTF, or it is damaged.");
        return nullptr;
    }
    auto book = std::make_shared<MemoryBook>();
    book->filePath = path;
    Rtf rtf(*book);
    rtf.parse(data);
    book->metadata.title = book->metadata.title.simplified();
    if (book->metadata.title.isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();
    if (rtf.chapters.isEmpty()) {
        if (error) *error = QStringLiteral("The document has no text.");
        return nullptr;
    }
    rtf.chapters.finish(*book);
    book->metadata.language = languageFromText(*book);
    return book;
}

bool DocFormat::canOpen(const QString& path, const QByteArray& head) const
{
    return hasSuffix(path, {"doc", "dot"}) || head.startsWith("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1");
}

std::shared_ptr<Book> DocFormat::open(const QString& path, QString* error) const
{
    const QByteArray data = readAll(path, error);
    if (data.isEmpty()) {
        if (error && error->isEmpty()) *error = QStringLiteral("The file is empty.");
        return nullptr;
    }
    // Many ".doc" files are really RTF or a web page.
    if (data.startsWith("{\\rtf")) return RtfFormat().open(path, error);
    const QByteArray head = data.left(1024).toLower();
    if (head.contains("<html") || head.contains("<!doctype html")) {
        auto book = std::make_shared<MemoryBook>();
        book->filePath = path;
        book->diskRoot = QFileInfo(path).absolutePath();
        html::addPage(*book, data, QFileInfo(path).fileName());
        if (book->metadata.title.isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();
        return book;
    }
    const QString text = wordText(data, error);
    if (text.trimmed().isEmpty()) {
        if (error && error->isEmpty()) *error = QStringLiteral("The document has no text.");
        return nullptr;
    }
    auto book = std::make_shared<MemoryBook>();
    book->filePath = path;
    // Each paragraph is one line: the plain-text reader keeps them apart and finds chapter headings.
    QString blankSeparated = text;
    blankSeparated.replace(QLatin1Char('\n'), QStringLiteral("\n\n"));
    blankSeparated.replace(QChar(0x2028), QLatin1Char('\n'));
    text::fromPlainText(*book, blankSeparated);
    if (book->metadata.title.isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();
    return book;
}

}  // namespace core

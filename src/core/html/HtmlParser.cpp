#include "HtmlParser.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

namespace dom {

namespace {

// HTML 4 named entities (the set books use), plus apos.
const QHash<QString, char32_t>& entityTable()
{
    static const QHash<QString, char32_t> table = [] {
        QHash<QString, char32_t> t;
        // Latin-1: code points 160..255 in order.
        static const char* latin1[] = {
            "nbsp", "iexcl", "cent", "pound", "curren", "yen", "brvbar", "sect", "uml", "copy", "ordf", "laquo", "not",
            "shy", "reg", "macr", "deg", "plusmn", "sup2", "sup3", "acute", "micro", "para", "middot", "cedil", "sup1",
            "ordm", "raquo", "frac14", "frac12", "frac34", "iquest", "Agrave", "Aacute", "Acirc", "Atilde", "Auml",
            "Aring", "AElig", "Ccedil", "Egrave", "Eacute", "Ecirc", "Euml", "Igrave", "Iacute", "Icirc", "Iuml", "ETH",
            "Ntilde", "Ograve", "Oacute", "Ocirc", "Otilde", "Ouml", "times", "Oslash", "Ugrave", "Uacute", "Ucirc",
            "Uuml", "Yacute", "THORN", "szlig", "agrave", "aacute", "acirc", "atilde", "auml", "aring", "aelig",
            "ccedil", "egrave", "eacute", "ecirc", "euml", "igrave", "iacute", "icirc", "iuml", "eth", "ntilde",
            "ograve", "oacute", "ocirc", "otilde", "ouml", "divide", "oslash", "ugrave", "uacute", "ucirc", "uuml",
            "yacute", "thorn", "yuml"};
        for (int i = 0; i < 96; ++i) t.insert(QString::fromLatin1(latin1[i]), char32_t(160 + i));
        static const struct { const char* n; char32_t c; } more[] = {
            {"amp", 38}, {"lt", 60}, {"gt", 62}, {"quot", 34}, {"apos", 39},
            {"OElig", 338}, {"oelig", 339}, {"Scaron", 352}, {"scaron", 353}, {"Yuml", 376}, {"fnof", 402},
            {"circ", 710}, {"tilde", 732}, {"ensp", 8194}, {"emsp", 8195}, {"thinsp", 8201}, {"zwnj", 8204},
            {"zwj", 8205}, {"lrm", 8206}, {"rlm", 8207}, {"ndash", 8211}, {"mdash", 8212}, {"lsquo", 8216},
            {"rsquo", 8217}, {"sbquo", 8218}, {"ldquo", 8220}, {"rdquo", 8221}, {"bdquo", 8222}, {"dagger", 8224},
            {"Dagger", 8225}, {"bull", 8226}, {"hellip", 8230}, {"permil", 8240}, {"prime", 8242}, {"Prime", 8243},
            {"lsaquo", 8249}, {"rsaquo", 8250}, {"oline", 8254}, {"frasl", 8260}, {"euro", 8364}, {"trade", 8482},
            {"larr", 8592}, {"uarr", 8593}, {"rarr", 8594}, {"darr", 8595}, {"harr", 8596}, {"minus", 8722},
            {"infin", 8734}, {"ne", 8800}, {"le", 8804}, {"ge", 8805}, {"loz", 9674}, {"spades", 9824},
            {"clubs", 9827}, {"hearts", 9829}, {"diams", 9830}, {"Alpha", 913}, {"Beta", 914}, {"Gamma", 915},
            {"Delta", 916}, {"Omega", 937}, {"alpha", 945}, {"beta", 946}, {"gamma", 947}, {"delta", 948},
            {"epsilon", 949}, {"pi", 960}, {"sigma", 963}, {"omega", 969}, {"hyphen", 8208}, {"nbhy", 8209},
        };
        for (const auto& m : more) t.insert(QString::fromLatin1(m.n), m.c);
        return t;
    }();
    return table;
}

const QSet<QString>& voidElements()
{
    static const QSet<QString> s = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta",
                                    "param", "source", "track", "wbr"};
    return s;
}

// Elements whose start tag closes an open <p>.
const QSet<QString>& closesParagraph()
{
    static const QSet<QString> s = {"address", "article", "aside", "blockquote", "div", "dl", "fieldset", "figure",
                                    "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "main",
                                    "nav", "ol", "p", "pre", "section", "table", "ul", "figcaption", "center"};
    return s;
}

// Elements an implied end tag never crosses.
const QSet<QString>& scopeBoundaries()
{
    static const QSet<QString> s = {"table", "td", "th", "caption", "html", "body", "div", "blockquote", "section",
                                    "article", "aside", "li", "ul", "ol", "dl"};
    return s;
}

QString cleanName(QStringView raw, ParseMode mode)
{
    QString n = raw.toString().toLower();
    const int colon = n.indexOf(QLatin1Char(':'));
    if (colon > 0) {
        const QStringView prefix = QStringView(n).left(colon);
        // Prefixes that only repeat the default namespace are dropped; others (dc:title) are kept.
        if (prefix == u"html" || prefix == u"xhtml" || prefix == u"svg" || prefix == u"opf" || prefix == u"ncx"
            || prefix == u"h" || (mode == ParseMode::Html && prefix != u"epub" && prefix != u"m" && prefix != u"math"))
            n = n.mid(colon + 1);
    }
    return n;
}

class Builder {
public:
    Builder(const QString& s, ParseMode mode) : m_s(s), m_mode(mode) {}

    std::unique_ptr<Document> run()
    {
        auto doc = std::make_unique<Document>();
        m_stack.push_back(doc->root.get());
        const qsizetype n = m_s.size();
        qsizetype i = 0;
        while (i < n) {
            if (m_s[i] == QLatin1Char('<')) {
                i = markup(i);
            } else {
                qsizetype j = m_s.indexOf(QLatin1Char('<'), i);
                if (j < 0) j = n;
                addText(decodeEntities(QStringView(m_s).mid(i, j - i)));
                i = j;
            }
        }
        return doc;
    }

private:
    Node* current() const { return m_stack.back(); }

    void addText(const QString& t)
    {
        if (t.isEmpty()) return;
        Node* p = current();
        if (!p->children.empty() && p->children.back()->isText()) {
            p->children.back()->text += t;
            return;
        }
        auto node = std::make_unique<Node>();
        node->type = Node::Text;
        node->text = t;
        append(p, std::move(node));
    }

    Node* append(Node* parent, std::unique_ptr<Node> node)
    {
        node->parent = parent;
        node->indexInParent = int(parent->children.size());
        if (node->isElement()) {
            int count = 0;
            for (const auto& c : parent->children)
                if (c->isElement()) ++count;
            node->elementIndex = count;
        }
        Node* raw = node.get();
        parent->children.push_back(std::move(node));
        return raw;
    }

    qsizetype markup(qsizetype i)
    {
        const qsizetype n = m_s.size();
        const QStringView rest = QStringView(m_s).mid(i);
        if (rest.startsWith(u"<!--")) {
            const qsizetype e = m_s.indexOf(QLatin1String("-->"), i + 4);
            return e < 0 ? n : e + 3;
        }
        if (rest.startsWith(u"<![CDATA[")) {
            const qsizetype e = m_s.indexOf(QLatin1String("]]>"), i + 9);
            addText(m_s.mid(i + 9, (e < 0 ? n : e) - i - 9));
            return e < 0 ? n : e + 3;
        }
        if (rest.startsWith(u"<!") || rest.startsWith(u"<?")) {
            const qsizetype e = m_s.indexOf(QLatin1Char('>'), i);
            return e < 0 ? n : e + 1;
        }
        if (rest.startsWith(u"</")) {
            qsizetype j = i + 2;
            while (j < n && m_s[j] != QLatin1Char('>')) ++j;
            endTag(cleanName(QStringView(m_s).mid(i + 2, j - i - 2).trimmed(), m_mode));
            return j < n ? j + 1 : n;
        }
        if (i + 1 < n && (m_s[i + 1].isLetter() || m_s[i + 1] == QLatin1Char('_'))) return startTag(i);
        addText(QStringLiteral("<"));
        return i + 1;
    }

    qsizetype startTag(qsizetype i)
    {
        const qsizetype n = m_s.size();
        qsizetype j = i + 1;
        while (j < n && !m_s[j].isSpace() && m_s[j] != QLatin1Char('>') && m_s[j] != QLatin1Char('/')) ++j;
        auto node = std::make_unique<Node>();
        node->name = cleanName(QStringView(m_s).mid(i + 1, j - i - 1), m_mode);
        bool selfClosing = false;
        // Attributes.
        while (j < n) {
            while (j < n && m_s[j].isSpace()) ++j;
            if (j >= n) break;
            if (m_s[j] == QLatin1Char('>')) { ++j; break; }
            if (m_s[j] == QLatin1Char('/')) {
                selfClosing = true;
                ++j;
                continue;
            }
            qsizetype k = j;
            while (k < n && !m_s[k].isSpace() && m_s[k] != QLatin1Char('=') && m_s[k] != QLatin1Char('>')
                   && !(m_s[k] == QLatin1Char('/') && k + 1 < n && m_s[k + 1] == QLatin1Char('>')))
                ++k;
            const QString name = m_s.mid(j, k - j).toLower();
            QString value;
            qsizetype v = k;
            while (v < n && m_s[v].isSpace()) ++v;
            if (v < n && m_s[v] == QLatin1Char('=')) {
                ++v;
                while (v < n && m_s[v].isSpace()) ++v;
                if (v < n && (m_s[v] == QLatin1Char('"') || m_s[v] == QLatin1Char('\''))) {
                    const QChar q = m_s[v];
                    qsizetype e = m_s.indexOf(q, v + 1);
                    if (e < 0) e = n;
                    value = decodeEntities(QStringView(m_s).mid(v + 1, e - v - 1));
                    k = e < n ? e + 1 : n;
                } else {
                    qsizetype e = v;
                    while (e < n && !m_s[e].isSpace() && m_s[e] != QLatin1Char('>')) ++e;
                    value = decodeEntities(QStringView(m_s).mid(v, e - v));
                    k = e;
                }
            }
            if (!name.isEmpty()) node->attrs.append({name, value});
            j = qMax(k, j + 1);
        }
        const QString name = node->name;
        if (m_mode == ParseMode::Html) impliedEnds(name);
        Node* added = append(current(), std::move(node));
        const bool isVoid = m_mode == ParseMode::Html && voidElements().contains(name);
        if (selfClosing || isVoid) return j;
        if (m_mode == ParseMode::Html && (name == QLatin1String("script") || name == QLatin1String("style"))) {
            // Raw text up to the matching end tag.
            const QRegularExpression endRe(QStringLiteral("</\\s*%1\\s*>").arg(name), QRegularExpression::CaseInsensitiveOption);
            const auto m = endRe.match(m_s, j);
            const qsizetype e = m.hasMatch() ? m.capturedStart() : n;
            if (name == QLatin1String("style")) {
                auto t = std::make_unique<Node>();
                t->type = Node::Text;
                t->text = m_s.mid(j, e - j);
                append(added, std::move(t));
            }
            return m.hasMatch() ? m.capturedEnd() : n;
        }
        m_stack.push_back(added);
        return j;
    }

    // Close elements that HTML closes implicitly when `name` starts.
    void impliedEnds(const QString& name)
    {
        auto closeNearest = [&](const QSet<QString>& names, const QSet<QString>& boundaries) {
            for (int k = int(m_stack.size()) - 1; k > 0; --k) {
                const QString& open = m_stack[size_t(k)]->name;
                if (names.contains(open)) {
                    m_stack.resize(size_t(k));
                    return;
                }
                if (boundaries.contains(open)) return;
            }
        };
        if (closesParagraph().contains(name)) closeNearest({QStringLiteral("p")}, scopeBoundaries());
        if (name == QLatin1String("li")) closeNearest({QStringLiteral("li")}, {"ul", "ol", "table", "body"});
        if (name == QLatin1String("dt") || name == QLatin1String("dd"))
            closeNearest({QStringLiteral("dt"), QStringLiteral("dd")}, {"dl", "table", "body"});
        if (name == QLatin1String("tr")) closeNearest({QStringLiteral("tr"), QStringLiteral("td"), QStringLiteral("th")}, {"table"});
        if (name == QLatin1String("td") || name == QLatin1String("th"))
            closeNearest({QStringLiteral("td"), QStringLiteral("th")}, {"tr", "table"});
        if (name == QLatin1String("tbody") || name == QLatin1String("thead") || name == QLatin1String("tfoot"))
            closeNearest({"tbody", "thead", "tfoot", "tr", "td", "th"}, {"table"});
    }

    void endTag(const QString& name)
    {
        for (int k = int(m_stack.size()) - 1; k > 0; --k) {
            if (m_stack[size_t(k)]->name == name) {
                m_stack.resize(size_t(k));
                return;
            }
            // An end tag never closes past a table cell or table in HTML mode (stray </div> inside a cell).
            if (m_mode == ParseMode::Html && (m_stack[size_t(k)]->is("td") || m_stack[size_t(k)]->is("th"))
                && name != QLatin1String("tr") && name != QLatin1String("table"))
                return;
        }
        // No matching open element: ignored.
    }

    const QString& m_s;
    ParseMode m_mode;
    std::vector<Node*> m_stack;
};

QString decodeBytes(const QByteArray& data)
{
    if (data.startsWith("\xEF\xBB\xBF")) return QString::fromUtf8(data.mid(3));
    if (data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF")) {
        QStringDecoder dec(data.startsWith("\xFF\xFE") ? QStringConverter::Utf16LE : QStringConverter::Utf16BE);
        return dec(data.mid(2));
    }
    // An encoding named in the XML declaration or a <meta> tag near the start.
    const QByteArray head = data.left(1024).toLower();
    QByteArray enc;
    static const QRegularExpression re(QStringLiteral("(?:encoding|charset)\\s*=\\s*[\"']?([a-z0-9_\\-]+)"));
    const auto m = re.match(QString::fromLatin1(head));
    if (m.hasMatch()) enc = m.captured(1).toLatin1();
    if (!enc.isEmpty() && enc != "utf-8" && enc != "utf8") {
        QStringDecoder dec(enc.constData());
        if (dec.isValid()) return dec(data);
    }
    return QString::fromUtf8(data);
}

}  // namespace

QString decodeEntities(QStringView s)
{
    if (!s.contains(u'&')) return s.toString();
    QString out;
    out.reserve(s.size());
    const qsizetype n = s.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = s[i];
        if (c != u'&') {
            out += c;
            continue;
        }
        qsizetype j = i + 1;
        while (j < n && j - i < 34 && (s[j].isLetterOrNumber() || s[j] == u'#')) ++j;
        const QStringView name = s.mid(i + 1, j - i - 1);
        const bool hasSemi = j < n && s[j] == u';';
        char32_t cp = 0;
        if (name.startsWith(u'#')) {
            bool ok = false;
            const uint v = (name.size() > 1 && (name[1] == u'x' || name[1] == u'X')) ? name.mid(2).toUInt(&ok, 16)
                                                                                       : name.mid(1).toUInt(&ok, 10);
            if (ok && v > 0 && v < 0x110000) cp = v;
        } else if (!name.isEmpty()) {
            cp = entityTable().value(name.toString(), 0);
        }
        if (cp) {
            out += QString::fromUcs4(&cp, 1);
            i = hasSemi ? j : j - 1;
        } else {
            out += c;
        }
    }
    return out;
}

std::unique_ptr<Document> parseText(const QString& text, ParseMode mode) { return Builder(text, mode).run(); }

std::unique_ptr<Document> parse(const QByteArray& data, ParseMode mode) { return parseText(decodeBytes(data), mode); }

}  // namespace dom

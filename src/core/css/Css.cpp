#include "Css.h"

#include "core/html/Dom.h"

#include <QUrl>

namespace css {

namespace {

QString stripComments(const QString& s)
{
    QString out;
    out.reserve(s.size());
    qsizetype i = 0;
    const qsizetype n = s.size();
    while (i < n) {
        if (s[i] == QLatin1Char('/') && i + 1 < n && s[i + 1] == QLatin1Char('*')) {
            const qsizetype e = s.indexOf(QLatin1String("*/"), i + 2);
            i = e < 0 ? n : e + 2;
            out += QLatin1Char(' ');
            continue;
        }
        out += s[i++];
    }
    return out;
}

// Index of the '}' matching the '{' at `open`, or the end.
qsizetype matchingBrace(QStringView s, qsizetype open)
{
    int depth = 0;
    QChar quote;
    for (qsizetype i = open; i < s.size(); ++i) {
        const QChar c = s[i];
        if (!quote.isNull()) {
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == u'"' || c == u'\'') quote = c;
        else if (c == u'{') ++depth;
        else if (c == u'}' && --depth == 0) return i;
    }
    return s.size();
}

bool mediaApplies(QStringView query)
{
    const QString q = query.toString().toLower();
    if (q.trimmed().isEmpty()) return true;
    // Kindle-specific, print and speech styles aren't for this reader.
    if (q.contains(QLatin1String("amzn-mobi")) || q.contains(QLatin1String("speech"))) return false;
    if (q.contains(QLatin1String("not ")) && q.contains(QLatin1String("amzn-kf8"))) return true;
    if (q.contains(QLatin1String("amzn-kf8"))) return false;
    if (q.contains(QLatin1String("print")) && !q.contains(QLatin1String("screen")) && !q.contains(QLatin1String("all")))
        return false;
    return true;
}

int weightValue(const QString& v)
{
    if (v == QLatin1String("bold") || v == QLatin1String("bolder")) return 700;
    if (v == QLatin1String("normal") || v == QLatin1String("lighter")) return 400;
    bool ok = false;
    const int n = v.toInt(&ok);
    return ok ? n : 400;
}

bool isNameChar(QChar c) { return c.isLetterOrNumber() || c == u'-' || c == u'_' || c.unicode() > 127 || c == u'\\'; }

}  // namespace

QString dirOf(const QString& path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

QString resolvePath(const QString& baseDir, const QString& relative)
{
    QString rel = QUrl::fromPercentEncoding(relative.toUtf8());
    const qsizetype hash = rel.indexOf(QLatin1Char('#'));
    if (hash >= 0) rel = rel.left(hash);
    if (rel.isEmpty()) return {};
    QStringList parts;
    if (!rel.startsWith(QLatin1Char('/')) && !baseDir.isEmpty()) parts = baseDir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& p : rel.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (p == QLatin1String(".")) continue;
        if (p == QLatin1String("..")) {
            if (!parts.isEmpty()) parts.removeLast();
            continue;
        }
        parts << p;
    }
    return parts.join(QLatin1Char('/'));
}

QString urlValue(QStringView v)
{
    QStringView s = v.trimmed();
    if (s.startsWith(u"url(", Qt::CaseInsensitive)) {
        const qsizetype e = s.indexOf(u')');
        s = s.mid(4, (e < 0 ? s.size() : e) - 4).trimmed();
    }
    if (s.size() >= 2 && (s.front() == u'"' || s.front() == u'\'') && s.back() == s.front()) s = s.mid(1, s.size() - 2);
    return s.toString();
}

std::vector<Declaration> parseDeclarations(QStringView s)
{
    std::vector<Declaration> out;
    qsizetype start = 0;
    int paren = 0;
    QChar quote;
    auto take = [&](qsizetype end) {
        const QStringView part = s.mid(start, end - start);
        const qsizetype colon = part.indexOf(u':');
        if (colon > 0) {
            Declaration d;
            d.property = part.left(colon).trimmed().toString().toLower();
            QString value = part.mid(colon + 1).trimmed().toString();
            const qsizetype bang = value.lastIndexOf(QLatin1Char('!'));
            if (bang >= 0 && value.mid(bang + 1).trimmed().compare(QLatin1String("important"), Qt::CaseInsensitive) == 0) {
                d.important = true;
                value = value.left(bang).trimmed();
            }
            d.value = value;
            if (!d.property.isEmpty() && !d.value.isEmpty()) out.push_back(std::move(d));
        }
        start = end + 1;
    };
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (!quote.isNull()) {
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == u'"' || c == u'\'') quote = c;
        else if (c == u'(') ++paren;
        else if (c == u')') --paren;
        else if (c == u';' && paren <= 0) take(i);
    }
    take(s.size());
    return out;
}

bool parseSelector(QStringView text, Selector* out)
{
    Selector sel;
    const QString t = text.trimmed().toString();
    const qsizetype n = t.size();
    qsizetype i = 0;
    Compound cur;
    bool haveCompound = false;
    char pendingComb = 0;
    int ids = 0, classes = 0, tags = 0;

    auto readName = [&](qsizetype& k) {
        const qsizetype b = k;
        while (k < n && (isNameChar(t[k]) || t[k] == u'|')) ++k;
        return t.mid(b, k - b);
    };
    auto finishCompound = [&] {
        if (!haveCompound) return;
        if (!sel.compounds.empty()) sel.combinators.push_back(pendingComb ? pendingComb : ' ');
        sel.compounds.push_back(cur);
        cur = Compound();
        haveCompound = false;
        pendingComb = 0;
    };

    while (i < n) {
        const QChar c = t[i];
        if (c.isSpace() || c == u'>' || c == u'+' || c == u'~') {
            char comb = ' ';
            while (i < n && (t[i].isSpace() || t[i] == u'>' || t[i] == u'+' || t[i] == u'~')) {
                if (t[i] != u' ' && !t[i].isSpace()) comb = char(t[i].toLatin1());
                ++i;
            }
            finishCompound();
            pendingComb = comb;
            continue;
        }
        if (c == u'*') {
            cur.tag = QStringLiteral("*");
            haveCompound = true;
            ++i;
        } else if (c == u'#') {
            ++i;
            cur.id = readName(i);
            haveCompound = true;
            ++ids;
        } else if (c == u'.') {
            ++i;
            cur.classes << readName(i);
            haveCompound = true;
            ++classes;
        } else if (c == u'[') {
            const qsizetype e = t.indexOf(QLatin1Char(']'), i);
            if (e < 0) return false;
            const QString inner = t.mid(i + 1, e - i - 1).trimmed();
            AttrSelector a;
            static const char* ops[] = {"~=", "|=", "^=", "$=", "*=", "="};
            qsizetype opAt = -1;
            for (const char* op : ops) {
                opAt = inner.indexOf(QLatin1String(op));
                if (opAt >= 0) {
                    a.op = op[1] == '=' && op[0] != '=' ? op[0] : '=';
                    a.name = inner.left(opAt).trimmed().toLower();
                    a.value = urlValue(QStringView(inner).mid(opAt + int(qstrlen(op))));
                    // [attr="x" i] case flag
                    if (a.value.endsWith(QLatin1String(" i"))) a.value.chop(2);
                    break;
                }
            }
            if (opAt < 0) a.name = inner.toLower();
            a.name.replace(QLatin1Char('|'), QLatin1Char(':'));
            if (a.name.startsWith(QLatin1Char(':'))) a.name.remove(0, 1);   // [|attr]
            cur.attrs.push_back(a);
            haveCompound = true;
            ++classes;
            i = e + 1;
        } else if (c == u':') {
            if (i + 1 < n && t[i + 1] == u':') return false;   // pseudo-elements: not generated
            ++i;
            const QString p = readName(i).toLower();
            static const QStringList supported = {"first-child", "last-child", "link", "visited"};
            if (!supported.contains(p)) return false;
            cur.pseudo << (p == QLatin1String("visited") ? QStringLiteral("link") : p);
            haveCompound = true;
            ++classes;
        } else if (isNameChar(c)) {
            QString name = readName(i).toLower();
            const qsizetype bar = name.indexOf(QLatin1Char('|'));
            if (bar >= 0) name = name.mid(bar + 1);   // ns|tag
            cur.tag = name;
            haveCompound = true;
            ++tags;
        } else {
            return false;   // something this parser doesn't know
        }
    }
    finishCompound();
    if (sel.compounds.empty()) return false;
    sel.specificity = ids * 10000 + classes * 100 + tags;
    *out = std::move(sel);
    return true;
}

namespace {

bool matchAttr(const AttrSelector& a, const dom::Node* el)
{
    if (!el->hasAttr(a.name.toLatin1().constData())) {
        // epub:type is sometimes written without its prefix in selectors and vice versa.
        return false;
    }
    const QString v = el->attr(a.name);
    switch (a.op) {
    case 0: return true;
    case '=': return v == a.value;
    case '~': return v.split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(a.value);
    case '|': return v == a.value || v.startsWith(a.value + QLatin1Char('-'));
    case '^': return !a.value.isEmpty() && v.startsWith(a.value);
    case '$': return !a.value.isEmpty() && v.endsWith(a.value);
    case '*': return !a.value.isEmpty() && v.contains(a.value);
    }
    return false;
}

bool matchCompound(const Compound& c, const dom::Node* el)
{
    if (!el || !el->isElement()) return false;
    if (!c.tag.isEmpty() && c.tag != QLatin1String("*") && c.tag != el->name) return false;
    if (!c.id.isEmpty() && el->id() != c.id) return false;
    if (!c.classes.isEmpty()) {
        const QString cls = el->attr("class");
        if (cls.isEmpty()) return false;
        const QStringList have = cls.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& k : c.classes)
            if (!have.contains(k)) return false;
    }
    for (const AttrSelector& a : c.attrs)
        if (!matchAttr(a, el)) return false;
    for (const QString& p : c.pseudo) {
        if (p == QLatin1String("first-child")) {
            if (el->previousElementSibling()) return false;
        } else if (p == QLatin1String("last-child")) {
            if (!el->parent) return false;
            for (size_t k = size_t(el->indexInParent) + 1; k < el->parent->children.size(); ++k)
                if (el->parent->children[k]->isElement()) return false;
        } else if (p == QLatin1String("link")) {
            if (!el->is("a") || !el->hasAttr("href")) return false;
        }
    }
    return true;
}

const dom::Node* parentElement(const dom::Node* n)
{
    const dom::Node* p = n->parent;
    return (p && p->isElement() && !p->name.isEmpty()) ? p : nullptr;
}

bool matchFrom(const Selector& s, int idx, const dom::Node* el)
{
    if (!matchCompound(s.compounds[size_t(idx)], el)) return false;
    if (idx == 0) return true;
    const char comb = s.combinators[size_t(idx - 1)];
    switch (comb) {
    case '>': return matchFrom(s, idx - 1, parentElement(el));
    case '+': return matchFrom(s, idx - 1, el->previousElementSibling());
    case '~':
        for (const dom::Node* p = el->previousElementSibling(); p; p = p->previousElementSibling())
            if (matchFrom(s, idx - 1, p)) return true;
        return false;
    default:
        for (const dom::Node* p = parentElement(el); p; p = parentElement(p))
            if (matchFrom(s, idx - 1, p)) return true;
        return false;
    }
}

}  // namespace

bool matches(const Selector& sel, const dom::Node* el)
{
    return !sel.compounds.empty() && matchFrom(sel, int(sel.compounds.size()) - 1, el);
}

void StyleSheet::parse(const QString& text, const QString& baseDir) { parseBlock(stripComments(text), baseDir); }

void StyleSheet::parseBlock(QStringView s, const QString& baseDir)
{
    qsizetype i = 0;
    const qsizetype n = s.size();
    while (i < n) {
        while (i < n && (s[i].isSpace() || s[i] == u';' || s[i] == u'}')) ++i;
        if (i >= n) break;
        if (s.mid(i).startsWith(u"<!--") || s.mid(i).startsWith(u"-->")) {
            i += s.mid(i).startsWith(u"<!--") ? 4 : 3;
            continue;
        }
        if (s[i] == u'@') {
            qsizetype k = i + 1;
            while (k < n && (s[k].isLetterOrNumber() || s[k] == u'-')) ++k;
            const QString name = s.mid(i + 1, k - i - 1).toString().toLower();
            // The rule's prelude ends at ';' or '{'.
            qsizetype e = k;
            while (e < n && s[e] != u';' && s[e] != u'{') ++e;
            if (e >= n) break;
            if (s[e] == u';') {
                i = e + 1;   // @import, @charset, @namespace
                continue;
            }
            const qsizetype close = matchingBrace(s, e);
            const QStringView body = s.mid(e + 1, close - e - 1);
            if (name == QLatin1String("media")) {
                if (mediaApplies(s.mid(k, e - k))) parseBlock(body, baseDir);
            } else if (name == QLatin1String("supports")) {
                parseBlock(body, baseDir);
            } else if (name == QLatin1String("font-face")) {
                FontFace f;
                for (const Declaration& d : parseDeclarations(body)) {
                    if (d.property == QLatin1String("font-family")) f.family = urlValue(d.value);
                    else if (d.property == QLatin1String("font-weight")) f.weight = weightValue(d.value.toLower());
                    else if (d.property == QLatin1String("font-style")) f.italic = d.value.toLower() != QLatin1String("normal");
                    else if (d.property == QLatin1String("src") && f.path.isEmpty()) {
                        // The first url(...) of the list; local(...) sources are skipped.
                        const qsizetype u = d.value.indexOf(QLatin1String("url("), 0, Qt::CaseInsensitive);
                        if (u >= 0) f.path = resolvePath(baseDir, urlValue(QStringView(d.value).mid(u)));
                    }
                }
                if (!f.family.isEmpty() && !f.path.isEmpty()) fontFaces.push_back(f);
            }
            // @page, @keyframes and the rest are ignored.
            i = close + 1;
            continue;
        }
        qsizetype brace = s.indexOf(u'{', i);
        if (brace < 0) break;
        const qsizetype close = matchingBrace(s, brace);
        const QStringView prelude = s.mid(i, brace - i);
        auto decls = std::make_shared<std::vector<Declaration>>(parseDeclarations(s.mid(brace + 1, close - brace - 1)));
        if (!decls->empty()) {
            for (QStringView part : prelude.split(u',')) {
                Rule r;
                if (!parseSelector(part, &r.selector)) continue;
                r.declarations = decls;
                r.order = m_order++;
                rules.push_back(std::move(r));
            }
        }
        i = close + 1;
    }
}

}  // namespace css

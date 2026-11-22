#include "Flow.h"

#include "Fonts.h"
#include "Layout.h"

#include "core/css/Css.h"
#include "core/format/Book.h"
#include "core/html/HtmlParser.h"

#include <QBuffer>
#include <QFontMetricsF>
#include <QImageReader>

#include <algorithm>

namespace layout {

namespace {

constexpr char16_t kLineSep = 0x2028;

struct CollapseState {
    bool lastSpace = true;      // the last character produced was a space or a line start
    bool pendingSpace = false;  // a collapsed space waits for the next visible character
};

// HTML whitespace rules. The same function builds the flow and maps positions back to the raw text, so the two
// always agree.
template <typename Out>
void collapse(QStringView raw, css::WhiteSpace ws, CollapseState& st, Out out)
{
    int pendingIndex = 0;   // raw index of the pending space (0 when it came from an earlier node)
    const bool keep = ws == css::WhiteSpace::Pre || ws == css::WhiteSpace::PreWrap;
    for (qsizetype i = 0; i < raw.size(); ++i) {
        const QChar c = raw[i];
        if (keep) {
            if (c == u'\r') continue;
            if (st.pendingSpace) {
                out(QChar(u' '), pendingIndex);
                st.pendingSpace = false;
            }
            out(c == u'\n' ? QChar(kLineSep) : c == u'\t' ? QChar(u' ') : c, int(i));
            st.lastSpace = c == u'\n';
            continue;
        }
        if (ws == css::WhiteSpace::PreLine && c == u'\n') {
            st.pendingSpace = false;
            out(QChar(kLineSep), int(i));
            st.lastSpace = true;
            continue;
        }
        const bool isWs = c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == u'\f';
        if (isWs) {
            if (!st.lastSpace && !st.pendingSpace) {
                st.pendingSpace = true;
                pendingIndex = int(i);
            }
            continue;
        }
        if (st.pendingSpace) {
            out(QChar(u' '), pendingIndex);
            st.pendingSpace = false;
        }
        out(c, int(i));
        st.lastSpace = false;
    }
}

QString romanNumeral(int n)
{
    static const struct { int v; const char* s; } t[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"},
                                                         {50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}};
    QString out;
    for (const auto& e : t)
        while (n >= e.v) {
            out += QLatin1String(e.s);
            n -= e.v;
        }
    return out;
}

QString markerText(css::ListStyle s, int n)
{
    switch (s) {
    case css::ListStyle::Disc: return QStringLiteral("•");
    case css::ListStyle::Circle: return QStringLiteral("◦");
    case css::ListStyle::Square: return QStringLiteral("▪");
    case css::ListStyle::Decimal: return QString::number(n) + QLatin1Char('.');
    case css::ListStyle::LowerAlpha: return QString(QChar(u'a' + (n - 1) % 26)) + QLatin1Char('.');
    case css::ListStyle::UpperAlpha: return QString(QChar(u'A' + (n - 1) % 26)) + QLatin1Char('.');
    case css::ListStyle::LowerRoman: return romanNumeral(n) + QLatin1Char('.');
    case css::ListStyle::UpperRoman: return romanNumeral(n).toUpper() + QLatin1Char('.');
    case css::ListStyle::None: return {};
    }
    return {};
}

bool isFootnoteAside(const dom::Node* e)
{
    if (!e->is("aside")) return false;
    for (const char* t : {"footnote", "endnote", "rearnote", "note"})
        if (e->attrHasWord("epub:type", QString::fromLatin1(t))) return true;
    const QString role = e->attr("role");
    return role == QLatin1String("doc-footnote") || role == QLatin1String("doc-endnote");
}

class Builder {
public:
    Builder(const core::Book& book, int spine, const FlowOptions& o) : m_book(book), m_spine(spine), m_o(o) {}

    std::shared_ptr<ChapterFlow> run()
    {
        m_flow = std::make_shared<ChapterFlow>();
        if (m_spine < 0 || m_spine >= int(m_book.spine.size())) return m_flow;
        const QString path = m_book.spine[size_t(m_spine)].path;
        m_dir = css::dirOf(path);
        bool ok = false;
        const QByteArray data = m_book.resource(path, &ok);
        if (!ok) return m_flow;
        const auto doc = dom::parse(data);
        const dom::Node* html = doc->documentElement();
        if (!html) return m_flow;

        std::vector<std::shared_ptr<const css::StyleSheet>> sheets;
        if (const dom::Node* head = doc->head()) {
            for (const auto& c : head->children) {
                if (c->is("link") && c->attr("rel").toLower().contains(QLatin1String("stylesheet"))
                    && !c->attr("rel").toLower().contains(QLatin1String("alternate"))) {
                    sheets.push_back(m_book.styleSheet(css::resolvePath(m_dir, c->attr("href"))));
                } else if (c->is("style")) {
                    auto s = std::make_shared<css::StyleSheet>();
                    s->parse(c->textContent(), m_dir);
                    sheets.push_back(s);
                }
            }
        }
        if (m_o.fonts)
            for (const auto& s : sheets) const_cast<FontResolver*>(m_o.fonts)->addFontFaces(m_book, *s);
        m_resolver = std::make_unique<css::StyleResolver>(sheets);

        m_ctx.out = &m_flow->blocks;
        const css::ComputedStyle htmlStyle = m_resolver->compute(html, css::StyleResolver::rootStyle());
        m_ctx.blockStyles.push_back(htmlStyle);
        pushFormat(htmlStyle, htmlStyle);
        walkChildren(html, htmlStyle, QString());
        flushPara();
        m_flow->length = m_offset;
        return m_flow;
    }

private:
    struct LinkOpen {
        int paraStart;
        int index;
        QString target;
        bool external;
        bool noteRefAttr;
    };
    struct Para {
        bool active = false;
        int start = 0;
        QString text;
        QList<QTextLayout::FormatRange> formats;
        int curFmt = -1;
        std::vector<LinkSpan> links;
        std::vector<InlineImage> inlineImages;
        css::ComputedStyle style;
        bool continuation = false;
        CollapseState cs;
    };
    struct Indent {
        float l = 0, r = 0, pl = 0, pr = 0;
    };
    struct Ctx {
        std::vector<Block>* out = nullptr;
        Indent indent;
        std::vector<Indent> indentStack;
        float pendingMargin = 0;
        bool pendingBreak = false;
        QString pendingMarker;
        Para para;
        std::vector<css::ComputedStyle> blockStyles;
        std::vector<int> listCounters;
    };

    float scale() const { return m_o.pxPerCssPx; }
    float px(const css::Len& l) const { return l.unit == css::Len::Px ? l.value * scale() : l.unit == css::Len::Percent ? l.value * 6.f * scale() : 0.f; }
    float pxH(const css::Len& l) const { return l.unit == css::Len::Px ? l.value * scale() : 0.f; }
    float pct(const css::Len& l) const { return l.unit == css::Len::Percent ? l.value : 0.f; }

    QFont fontFor(const css::ComputedStyle& s, float cssPx) const
    {
        QFont f;
        f.setFamilies(m_o.fonts ? m_o.fonts->resolve(s.fontFamilies) : s.fontFamilies);
        f.setPixelSize(qMax(1, qRound(cssPx * scale())));
        f.setWeight(QFont::Weight(qBound(100, s.fontWeight, 900)));
        f.setItalic(s.italic);
        switch (s.caps) {
        case css::ComputedStyle::SmallCaps: f.setCapitalization(QFont::SmallCaps); break;
        case css::ComputedStyle::Upper: f.setCapitalization(QFont::AllUppercase); break;
        case css::ComputedStyle::Lower: f.setCapitalization(QFont::AllLowercase); break;
        case css::ComputedStyle::Capitalize: f.setCapitalization(QFont::Capitalize); break;
        default: break;
        }
        f.setHintingPreference(QFont::PreferNoHinting);
        f.setKerning(true);
        return f;
    }

    void pushFormat(const css::ComputedStyle& s, const css::ComputedStyle& parent)
    {
        QTextCharFormat fmt;
        const bool shifted = s.verticalAlign != css::VAlign::Baseline;
        // Qt shrinks super- and subscripts itself; start from the surrounding size so they aren't shrunk twice.
        fmt.setFont(fontFor(s, shifted ? parent.fontSize : s.fontSize), QTextCharFormat::FontPropertiesAll);
        if (s.underline) fmt.setFontUnderline(true);
        if (s.strike) fmt.setFontStrikeOut(true);
        if (s.verticalAlign == css::VAlign::Super) fmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
        if (s.verticalAlign == css::VAlign::Sub) fmt.setVerticalAlignment(QTextCharFormat::AlignSubScript);
        m_formats.push_back(fmt);
        m_formatIds.push_back(m_nextFormatId++);
    }
    void popFormat()
    {
        m_formats.pop_back();
        m_formatIds.pop_back();
    }

    int currentOffset() const { return m_ctx.para.active ? m_ctx.para.start + int(m_ctx.para.text.size()) : m_offset; }

    void recordId(const dom::Node* e)
    {
        const QString id = e->id();
        if (!id.isEmpty() && !m_flow->ids.contains(id)) m_flow->ids.insert(id, currentOffset());
        if (e->is("a")) {
            const QString name = e->attr("name");
            if (!name.isEmpty() && !m_flow->ids.contains(name)) m_flow->ids.insert(name, currentOffset());
        }
    }
    void recordIdsDeep(const dom::Node* e)
    {
        if (e->isElement()) recordId(e);
        for (const auto& c : e->children) recordIdsDeep(c.get());
    }

    void startPara()
    {
        Para& p = m_ctx.para;
        p = Para();
        p.active = true;
        p.start = m_offset;
        p.style = m_ctx.blockStyles.back();
        p.continuation = m_continuation;
        m_continuation = false;
    }

    void appendChar(QChar ch)
    {
        Para& p = m_ctx.para;
        const int id = m_formatIds.back();
        if (p.curFmt != id) {
            QTextLayout::FormatRange r;
            r.start = int(p.text.size());
            r.length = 0;
            r.format = m_formats.back();
            p.formats.push_back(r);
            p.curFmt = id;
        }
        p.text += ch;
        p.formats.back().length++;
    }

    void appendText(const dom::Node* t, const css::ComputedStyle& style, const QString& path)
    {
        const css::WhiteSpace ws = style.whiteSpace;
        const bool keep = ws == css::WhiteSpace::Pre || ws == css::WhiteSpace::PreWrap;
        if (!m_ctx.para.active) {
            if (!keep) {
                bool allSpace = true;
                for (QChar c : t->text)
                    if (!(c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == u'\f')) {
                        allSpace = false;
                        break;
                    }
                if (allSpace) return;
            }
            startPara();
        }
        Para& p = m_ctx.para;
        TextNodeRef ref;
        ref.path = path;
        ref.start = p.start + int(p.text.size());
        ref.raw = t->text;
        ref.lastSpace = p.cs.lastSpace;
        ref.pendingSpace = p.cs.pendingSpace;
        ref.whiteSpace = ws;
        collapse(t->text, ws, p.cs, [&](QChar c, int) { appendChar(c); });
        ref.length = p.start + int(p.text.size()) - ref.start;
        if (ref.length > 0) m_flow->textNodes.push_back(std::move(ref));
    }

    void lineBreak()
    {
        if (!m_ctx.para.active) startPara();
        m_ctx.para.cs.pendingSpace = false;
        appendChar(QChar(kLineSep));
        m_ctx.para.cs.lastSpace = true;
    }

    void fillBox(Block& b)
    {
        b.spaceBefore = m_ctx.pendingMargin;
        b.marginLeft = m_ctx.indent.l;
        b.marginRight = m_ctx.indent.r;
        b.marginLeftPct = m_ctx.indent.pl;
        b.marginRightPct = m_ctx.indent.pr;
        b.breakBefore = m_ctx.pendingBreak;
        b.chapterStart = m_firstBlock;
        m_ctx.pendingMargin = 0;
        m_ctx.pendingBreak = false;
        m_firstBlock = false;
    }

    void flushPara()
    {
        Para& p = m_ctx.para;
        if (!p.active) return;
        p.active = false;
        // A <br> at the very end of a paragraph doesn't add an empty line.
        while (!p.text.isEmpty() && p.text.back() == QChar(kLineSep)) {
            p.text.chop(1);
            if (!p.formats.isEmpty()) {
                if (--p.formats.back().length <= 0) p.formats.removeLast();
            }
        }
        if (p.text.isEmpty()) return;
        Block b;
        b.kind = Block::Text;
        b.start = p.start;
        b.length = int(p.text.size());
        b.text = p.text;
        b.formats = p.formats;
        b.links = std::move(p.links);
        b.inlineImages = std::move(p.inlineImages);
        b.marker = m_ctx.pendingMarker;
        m_ctx.pendingMarker.clear();
        b.align = p.style.align;
        b.indent = p.continuation ? 0.f : p.style.textIndent * scale();
        b.lineHeight = p.style.lineHeight;
        b.fontPx = p.style.fontSize * scale();
        b.font = fontFor(p.style, p.style.fontSize);
        b.avoidInside = p.style.avoidBreakInside;
        fillBox(b);
        m_ctx.out->push_back(std::move(b));
        m_offset = p.start + int(p.text.size()) + 1;
    }

    void enterBlock(const css::ComputedStyle& s, const dom::Node* e)
    {
        flushPara();
        m_ctx.pendingMargin = qMax(m_ctx.pendingMargin, pxH(s.marginTop) + (s.marginTop.unit == css::Len::Percent ? px(s.marginTop) : 0.f))
                              + pxH(s.paddingTop);
        if (s.breakBefore) m_ctx.pendingBreak = true;
        m_ctx.indentStack.push_back(m_ctx.indent);
        // The reader sets the page margins: side margins of the whole document (html, body) are ignored.
        if (!e->is("html") && !e->is("body")) {
            m_ctx.indent.l += px(s.marginLeft.unit == css::Len::Percent ? css::Len() : s.marginLeft) + px(s.paddingLeft);
            m_ctx.indent.r += px(s.marginRight.unit == css::Len::Percent ? css::Len() : s.marginRight) + px(s.paddingRight);
            m_ctx.indent.pl += pct(s.marginLeft);
            m_ctx.indent.pr += pct(s.marginRight);
        }
        m_ctx.blockStyles.push_back(s);
        if (e->is("ol") || e->is("ul")) {
            bool ok = false;
            const int start = e->attr("start").toInt(&ok);
            m_ctx.listCounters.push_back(ok ? start - 1 : 0);
        }
        if (s.display == css::Display::ListItem) {
            if (m_ctx.listCounters.empty()) m_ctx.listCounters.push_back(0);
            bool ok = false;
            const int v = e->attr("value").toInt(&ok);
            int& counter = m_ctx.listCounters.back();
            counter = ok ? v : counter + 1;
            m_ctx.pendingMarker = markerText(s.listStyle, counter);
        }
    }

    void leaveBlock(const css::ComputedStyle& s, const dom::Node* e, size_t blocksAtEnter)
    {
        flushPara();
        if (s.avoidBreakAfter && m_ctx.out->size() > blocksAtEnter) m_ctx.out->back().keepWithNext = true;
        if (s.avoidBreakInside)
            for (size_t i = blocksAtEnter; i < m_ctx.out->size(); ++i) (*m_ctx.out)[i].avoidInside = true;
        m_ctx.pendingMargin = qMax(m_ctx.pendingMargin, pxH(s.marginBottom)) + pxH(s.paddingBottom);
        if (s.breakAfter) m_ctx.pendingBreak = true;
        m_ctx.indent = m_ctx.indentStack.back();
        m_ctx.indentStack.pop_back();
        m_ctx.blockStyles.pop_back();
        if (e->is("ol") || e->is("ul")) {
            if (!m_ctx.listCounters.empty()) m_ctx.listCounters.pop_back();
        }
        if (s.display == css::Display::ListItem) m_ctx.pendingMarker.clear();
    }

    // A small image inside a line of text. The decision uses only the image's own size and the CSS font size
    // (never the reader's text size), so offsets stay the same at every text size.
    bool inlineImage(const dom::Node* e, const css::ComputedStyle& s, const QString& path)
    {
        bool ok = false;
        const QByteArray data = m_book.resource(path, &ok);
        if (!ok) return false;
        QBuffer buf;
        buf.setData(data);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf);
        QSizeF natural = reader.size();
        if (s.width.unit == css::Len::Px && s.height.unit == css::Len::Px) natural = QSizeF(s.width.value, s.height.value);
        if (natural.isEmpty()) return false;
        const bool hasText = m_ctx.para.active && !m_ctx.para.text.trimmed().isEmpty();
        const float limit = (hasText ? 4.f : 2.f) * s.fontSize;
        if (natural.height() > limit) return false;

        if (!m_ctx.para.active) startPara();
        Para& p = m_ctx.para;
        if (p.cs.pendingSpace) {
            appendChar(QChar(u' '));
            p.cs.pendingSpace = false;
        }
        QSizeF size = natural * scale();
        const float cap = 1.2f * s.fontSize * scale();
        if (size.height() > cap) size *= cap / size.height();
        QTextCharFormat fmt = m_formats.back();
        QFont f = fmt.font();
        qreal advance = 0;
        {
            QMutexLocker lock(&fontEngineLock());
            advance = QFontMetricsF(f).horizontalAdvance(QChar(0x2007));
        }
        f.setLetterSpacing(QFont::AbsoluteSpacing, qMax<qreal>(0, size.width() - advance + 2));
        fmt.setFont(f, QTextCharFormat::FontPropertiesAll);
        QTextLayout::FormatRange r;
        r.start = int(p.text.size());
        r.length = 1;
        r.format = fmt;
        p.formats.push_back(r);
        p.curFmt = -2;   // the next character starts a new range
        InlineImage im;
        im.index = int(p.text.size());
        im.size = size;
        im.path = path;
        if (m_o.fonts) im.image = QImage::fromData(data).scaled((size * 2).toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.text += QChar(0x2007);
        p.cs.lastSpace = false;
        p.inlineImages.push_back(std::move(im));
        (void)e;
        return true;
    }

    void image(const dom::Node* e, const css::ComputedStyle& s, const QString& src)
    {
        if (inlineImage(e, s, css::resolvePath(m_dir, src))) return;
        const bool hadText = m_ctx.para.active && !m_ctx.para.text.trimmed().isEmpty();
        flushPara();
        if (hadText) m_continuation = true;
        Block b;
        b.kind = Block::Image;
        b.start = m_offset;
        b.length = 1;
        b.imagePath = css::resolvePath(m_dir, src);
        b.alt = e->attr("alt");
        b.width = s.width;
        b.height = s.height;
        if (b.width.unit == css::Len::Px) b.width.value *= scale();
        if (b.height.unit == css::Len::Px) b.height.value *= scale();
        fillBox(b);
        m_ctx.out->push_back(std::move(b));
        m_offset += 2;
        if (m_ctx.para.active == false && hadText) m_continuation = true;
    }

    void rule(const css::ComputedStyle&)
    {
        flushPara();
        Block b;
        b.kind = Block::Rule;
        b.start = m_offset;
        b.length = 1;
        fillBox(b);
        m_ctx.out->push_back(std::move(b));
        m_offset += 2;
    }

    void table(const dom::Node* e, const css::ComputedStyle& s, const QString& path)
    {
        flushPara();
        // The caption goes above the table, as an ordinary centered paragraph.
        int idx = 0;
        for (const auto& c : e->children) {
            if (c->isElement()) ++idx;
            if (c->is("caption")) element(c.get(), s, path + QLatin1Char('/') + QString::number(2 * idx));
        }
        flushPara();
        m_ctx.pendingMargin = qMax(m_ctx.pendingMargin, pxH(s.marginTop));
        if (s.breakBefore) m_ctx.pendingBreak = true;
        auto t = std::make_shared<Table>();
        t->borders = s.border;
        t->cellPadX = 0.4f * s.fontSize * scale();
        t->cellPadY = 0.2f * s.fontSize * scale();
        Block tb;
        tb.kind = Block::Table;
        tb.start = m_offset;
        tb.table = t;
        fillBox(tb);
        tb.marginLeft += px(s.marginLeft.unit == css::Len::Percent ? css::Len() : s.marginLeft);
        tb.width = s.width;
        if (tb.width.unit == css::Len::Px) tb.width.value *= scale();
        rows(e, s, path, *t, false);
        tb.length = qMax(1, m_offset - tb.start);
        m_offset = tb.start + tb.length + 1;
        m_ctx.out->push_back(std::move(tb));
        m_ctx.pendingMargin = pxH(s.marginBottom);
    }

    void rows(const dom::Node* n, const css::ComputedStyle& parentStyle, const QString& path, Table& t, bool head)
    {
        int idx = 0;
        for (const auto& c : n->children) {
            if (!c->isElement()) continue;
            ++idx;
            const QString p = path + QLatin1Char('/') + QString::number(2 * idx) + idAssertion(c.get());
            const css::ComputedStyle s = m_resolver->compute(c.get(), parentStyle);
            if (s.display == css::Display::None || c->is("caption") || s.display == css::Display::TableColumn) continue;
            if (s.display == css::Display::TableRowGroup || c->is("thead") || c->is("tbody") || c->is("tfoot")) {
                recordId(c.get());
                rows(c.get(), s, p, t, head || c->is("thead"));
            } else if (s.display == css::Display::TableRow || c->is("tr")) {
                recordId(c.get());
                row(c.get(), s, p, t, head);
            }
        }
    }

    void row(const dom::Node* tr, const css::ComputedStyle& rowStyle, const QString& path, Table& t, bool head)
    {
        TableRow r;
        int idx = 0;
        for (const auto& c : tr->children) {
            if (!c->isElement()) continue;
            ++idx;
            const css::ComputedStyle s = m_resolver->compute(c.get(), rowStyle);
            if (s.display == css::Display::None) continue;
            const QString p = path + QLatin1Char('/') + QString::number(2 * idx) + idAssertion(c.get());
            TableCell cell;
            cell.colspan = qBound(1, c->attr("colspan").toInt(), 50);
            cell.header = head || c->is("th");
            if (s.border) t.borders = true;
            Ctx saved = std::move(m_ctx);
            m_ctx = Ctx();
            m_ctx.out = &cell.blocks;
            m_ctx.blockStyles.push_back(s);
            recordId(c.get());
            pushFormat(s, rowStyle);
            walkChildren(c.get(), s, p);
            flushPara();
            popFormat();
            m_ctx = std::move(saved);
            r.cells.push_back(std::move(cell));
        }
        if (!r.cells.empty()) t.rows.push_back(std::move(r));
    }

    static QString idAssertion(const dom::Node* e)
    {
        const QString id = e->id();
        return id.isEmpty() ? QString() : QLatin1Char('[') + id + QLatin1Char(']');
    }

    void walkChildren(const dom::Node* n, const css::ComputedStyle& style, const QString& path)
    {
        int elementsSeen = 0;
        for (const auto& c : n->children) {
            if (c->isText()) {
                appendText(c.get(), style, path + QLatin1Char('/') + QString::number(2 * elementsSeen + 1));
            } else {
                ++elementsSeen;
                element(c.get(), style, path + QLatin1Char('/') + QString::number(2 * elementsSeen) + idAssertion(c.get()));
            }
        }
    }

    void element(const dom::Node* e, const css::ComputedStyle& parentStyle, const QString& path)
    {
        if (e->is("head") || e->is("script") || e->is("style") || e->is("title")) return;
        const css::ComputedStyle s = m_resolver->compute(e, parentStyle);
        if (s.display == css::Display::None || isFootnoteAside(e)) {
            recordIdsDeep(e);
            return;
        }
        recordId(e);
        if (e->is("br")) {
            lineBreak();
            return;
        }
        if (e->is("img")) {
            const QString src = e->attr("src");
            if (!src.isEmpty()) image(e, s, src);
            return;
        }
        if (e->is("svg")) {
            if (const dom::Node* img = e->findDescendant("image")) {
                QString href = img->attr("xlink:href");
                if (href.isEmpty()) href = img->attr("href");
                if (!href.isEmpty()) image(img, s, href);
            }
            return;
        }
        if (e->is("hr")) {
            rule(s);
            return;
        }
        if (s.display == css::Display::Table || e->is("table")) {
            table(e, s, path);
            return;
        }
        const bool block = s.isBlockLevel();
        const size_t blocksAtEnter = m_ctx.out->size();
        pushFormat(s, parentStyle);
        const bool isLink = e->is("a") && e->hasAttr("href");
        if (isLink) {
            const QString href = e->attr("href").trimmed();
            LinkOpen l;
            l.paraStart = m_ctx.para.active ? m_ctx.para.start : m_offset;
            l.index = m_ctx.para.active ? int(m_ctx.para.text.size()) : 0;
            l.external = href.contains(QLatin1String("://")) || href.startsWith(QLatin1String("mailto:"));
            if (l.external) {
                l.target = href;
            } else if (href.startsWith(QLatin1Char('#'))) {
                l.target = m_book.spine[size_t(m_spine)].path + href;
            } else {
                const qsizetype hash = href.indexOf(QLatin1Char('#'));
                l.target = css::resolvePath(m_dir, hash >= 0 ? href.left(hash) : href) + (hash >= 0 ? href.mid(hash) : QString());
            }
            l.noteRefAttr = e->attrHasWord("epub:type", QStringLiteral("noteref")) || e->attr("role") == QLatin1String("doc-noteref");
            m_links.push_back(l);
        }
        if (block) enterBlock(s, e);
        walkChildren(e, s, path);
        if (block) leaveBlock(s, e, blocksAtEnter);
        if (isLink) {
            const LinkOpen l = m_links.back();
            m_links.pop_back();
            Para& p = m_ctx.para;
            if (p.active && p.start == l.paraStart && int(p.text.size()) > l.index) {
                LinkSpan span;
                span.start = l.index;
                span.end = int(p.text.size());
                span.target = l.target;
                span.external = l.external;
                const QString t = p.text.mid(span.start, span.end - span.start).trimmed();
                bool shortMark = !t.isEmpty() && t.size() <= 5;
                for (QChar c : t)
                    if (c.isLetter() && !(c == u'i' || c == u'v' || c == u'x')) shortMark = false;
                span.noteRef = l.noteRefAttr || (shortMark && !l.external) || s.verticalAlign == css::VAlign::Super;
                p.links.push_back(span);
            }
        }
        popFormat();
    }

    const core::Book& m_book;
    int m_spine;
    FlowOptions m_o;
    QString m_dir;
    std::unique_ptr<css::StyleResolver> m_resolver;
    std::shared_ptr<ChapterFlow> m_flow;
    int m_offset = 0;
    bool m_firstBlock = true;
    bool m_continuation = false;
    Ctx m_ctx;
    std::vector<QTextCharFormat> m_formats;
    std::vector<int> m_formatIds;
    int m_nextFormatId = 0;
    std::vector<LinkOpen> m_links;
};

void appendRange(const std::vector<Block>& blocks, int from, int to, QString& out)
{
    for (const Block& b : blocks) {
        if (b.start + b.length < from || b.start >= to) continue;
        if (b.kind == Block::Text) {
            const int s = qMax(0, from - b.start), e = qMin(b.length, to - b.start);
            if (e <= s) continue;
            if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n'))) out += QLatin1Char('\n');
            QString t = b.text.mid(s, e - s);
            t.replace(QChar(kLineSep), QLatin1Char('\n'));
            t.remove(QChar(0x00AD));
            t.remove(QChar(0x2007));   // inline images
            out += t;
        } else if (b.kind == Block::Table && b.table) {
            for (const TableRow& r : b.table->rows) {
                QString line;
                for (const TableCell& c : r.cells) {
                    QString cellText;
                    appendRange(c.blocks, from, to, cellText);
                    if (!line.isEmpty() || !cellText.isEmpty()) line += (line.isEmpty() ? QString() : QStringLiteral("\t")) + cellText.simplified();
                }
                if (!line.trimmed().isEmpty()) {
                    if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n'))) out += QLatin1Char('\n');
                    out += line;
                }
            }
        }
    }
}

}  // namespace

QString ChapterFlow::plainText() const { return textRange(0, length); }

QString ChapterFlow::textRange(int from, int to) const
{
    QString out;
    appendRange(blocks, from, to, out);
    return out;
}

std::shared_ptr<ChapterFlow> buildFlow(const core::Book& book, int spineIndex, const FlowOptions& options)
{
    return Builder(book, spineIndex, options).run();
}

bool rawOffsetFor(const ChapterFlow& flow, int offset, const TextNodeRef** node, int* rawOffset)
{
    const auto& v = flow.textNodes;
    if (v.empty()) return false;
    auto it = std::upper_bound(v.begin(), v.end(), offset, [](int o, const TextNodeRef& r) { return o < r.start; });
    if (it == v.begin()) {
        *node = &v.front();
        *rawOffset = 0;
        return true;
    }
    --it;
    const TextNodeRef& r = *it;
    *node = &r;
    const int k = offset - r.start;
    if (k >= r.length) {
        *rawOffset = int(r.raw.size());
        return true;
    }
    CollapseState st{r.lastSpace, r.pendingSpace};
    int count = 0, found = -1;
    collapse(r.raw, r.whiteSpace, st, [&](QChar, int rawIndex) {
        if (count == k && found < 0) found = rawIndex;
        ++count;
    });
    *rawOffset = found < 0 ? int(r.raw.size()) : found;
    return true;
}

int flowOffsetFor(const TextNodeRef& node, int rawOffset)
{
    CollapseState st{node.lastSpace, node.pendingSpace};
    int count = 0;
    collapse(node.raw, node.whiteSpace, st, [&](QChar, int rawIndex) {
        if (rawIndex < rawOffset) ++count;
    });
    return node.start + qMin(count, node.length);
}

}  // namespace layout

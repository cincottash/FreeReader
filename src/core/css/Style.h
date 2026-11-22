// Computed styles: the cascade (built-in reader defaults, the book's style sheets, inline style="") applied to one
// element, with inheritance from its parent. Sizes are in CSS pixels at a 16 px base; the layout scales them to
// the reader's chosen text size.
#pragma once

#include "Css.h"

#include <QHash>
#include <QStringList>

#include <memory>
#include <vector>

namespace css {

enum class Display : quint8 { Inline, Block, ListItem, None, Table, TableRow, TableCell, TableRowGroup, TableCaption, TableColumn };
enum class Align : quint8 { Start, Left, Right, Center, Justify };
enum class WhiteSpace : quint8 { Normal, Pre, PreWrap, PreLine, NoWrap };
enum class VAlign : quint8 { Baseline, Super, Sub };
enum class ListStyle : quint8 { Disc, Circle, Square, Decimal, LowerAlpha, UpperAlpha, LowerRoman, UpperRoman, None };

struct Len {
    enum Unit : quint8 { Px, Percent, Auto };
    float value = 0;
    Unit unit = Px;
    bool isAuto() const { return unit == Auto; }
    float px(float containerWidth) const { return unit == Px ? value : unit == Percent ? value * containerWidth / 100.f : 0.f; }
};

struct ComputedStyle {
    // Inherited
    QStringList fontFamilies;          // as written in the book ("Georgia", "serif")
    float fontSize = 16;               // CSS px
    int fontWeight = 400;
    bool italic = false;
    enum Caps : quint8 { CapsNormal, SmallCaps, Upper, Lower, Capitalize };
    Caps caps = CapsNormal;            // font-variant: small-caps and text-transform (drawn, the text isn't changed)
    Align align = Align::Start;
    float textIndent = 0;              // CSS px
    float lineHeight = 0;              // multiplier; 0 = normal
    WhiteSpace whiteSpace = WhiteSpace::Normal;
    bool hyphens = true;
    bool underline = false;
    bool strike = false;
    ListStyle listStyle = ListStyle::Disc;

    // Not inherited
    Display display = Display::Inline;
    Len marginTop, marginBottom, marginLeft, marginRight;
    Len paddingTop, paddingBottom, paddingLeft, paddingRight;
    Len width{0, Len::Auto}, height{0, Len::Auto}, maxWidth{0, Len::Auto};
    VAlign verticalAlign = VAlign::Baseline;
    bool breakBefore = false, breakAfter = false, avoidBreakAfter = false, avoidBreakInside = false;
    bool border = false;               // any visible border (tables draw cell lines)
    bool marginLeftAuto = false, marginRightAuto = false;   // centered blocks (images, tables)

    bool isBlockLevel() const
    {
        return display == Display::Block || display == Display::ListItem || display == Display::Table
               || display == Display::TableCaption || display == Display::TableRow || display == Display::TableCell
               || display == Display::TableRowGroup;
    }
};

class StyleResolver {
public:
    // Sheets in cascade order (the first is the built-in reader style sheet, origin "user agent").
    explicit StyleResolver(std::vector<std::shared_ptr<const StyleSheet>> authorSheets);

    ComputedStyle compute(const dom::Node* el, const ComputedStyle& parent) const;
    static ComputedStyle rootStyle();
    static std::shared_ptr<const StyleSheet> userAgentSheet();

private:
    struct Indexed {
        const Rule* rule;
        int origin;   // 0 = user agent, 1 = book
    };
    void index(const StyleSheet& sheet, int origin);
    void collect(const QString& key, const dom::Node* el, std::vector<std::pair<const Indexed*, const Declaration*>>& out) const;

    std::vector<std::shared_ptr<const StyleSheet>> m_sheets;
    QHash<QString, std::vector<Indexed>> m_buckets;   // "#id", ".class", "tag", "*"
};

// Exposed for tests.
bool parseLength(const QString& v, float fontSize, Len* out);

}  // namespace css

// Style sheets for book chapters: the CSS subset readers need (see docs: fonts, alignment, margins, indents, lists,
// tables). Rules with selectors this parser can't match (pseudo-elements, :nth-child) are dropped rather than
// misapplied.
#pragma once

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace dom {
struct Node;
}

namespace css {

struct Declaration {
    QString property;   // lowercase
    QString value;      // trimmed, without !important
    bool important = false;
};

struct AttrSelector {
    QString name;       // lowercase; "epub|type" is stored as "epub:type"
    QString value;
    char op = 0;        // 0 = exists, '=', '~' (word), '|', '^', '$', '*'
};

struct Compound {
    QString tag;        // empty or "*" = any
    QString id;
    QStringList classes;
    std::vector<AttrSelector> attrs;
    QStringList pseudo; // supported pseudo-classes: first-child, last-child, link
};

struct Selector {
    std::vector<Compound> compounds;   // left to right
    std::vector<char> combinators;     // between compounds: ' ', '>', '+', '~'
    int specificity = 0;
};

struct Rule {
    Selector selector;
    std::shared_ptr<const std::vector<Declaration>> declarations;
    int order = 0;
};

struct FontFace {
    QString family;
    int weight = 400;
    bool italic = false;
    QString path;       // the font file's path in the book, resolved against the style sheet's folder
};

class StyleSheet {
public:
    // baseDir: the folder of the style sheet inside the book ("OEBPS/css"), for url(...) values.
    void parse(const QString& text, const QString& baseDir = QString());

    std::vector<Rule> rules;
    std::vector<FontFace> fontFaces;

private:
    void parseBlock(QStringView s, const QString& baseDir);
    int m_order = 0;
};

std::vector<Declaration> parseDeclarations(QStringView s);
bool parseSelector(QStringView text, Selector* out);
bool matches(const Selector& sel, const dom::Node* el);

// "a/b/../c.css" + "img/x.png" style joins, inside the book's zip.
QString resolvePath(const QString& baseDir, const QString& relative);
QString dirOf(const QString& path);
// The address in a CSS url(...) value, or the value itself without quotes.
QString urlValue(QStringView v);

}  // namespace css

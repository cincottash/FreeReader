// Font choice for book text: the book's embedded fonts (@font-face), the reader's defaults for the generic
// families, and fallbacks so every family list ends in a font this computer has.
#pragma once

#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <memory>

namespace core {
class Book;
}
namespace css {
class StyleSheet;
}

namespace layout {

class FontResolver {
public:
    FontResolver();
    ~FontResolver();   // removes the fonts it added to the application

    // Registers the @font-face fonts of a style sheet (once per font file). Thread-safe.
    void addFontFaces(const core::Book& book, const css::StyleSheet& sheet);
    // Qt family names for a CSS font-family list.
    QStringList resolve(const QStringList& cssFamilies) const;

    QStringList serif() const { return m_serif; }
    QStringList sansSerif() const { return m_sans; }
    QStringList monospace() const { return m_mono; }
    void setBodyFamilies(const QStringList& f) { m_body = f; }   // the reader's text font (empty = book's)

private:
    mutable QMutex m_mutex;
    QHash<QString, QString> m_embedded;   // CSS family (lowercase) -> Qt family
    QHash<QString, bool> m_loadedFiles;
    QList<int> m_fontIds;
    QStringList m_serif, m_sans, m_mono, m_body;
};

}  // namespace layout

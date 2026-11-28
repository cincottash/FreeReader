// How books look: text size, spacing, margins, page width, columns, justification, hyphenation and font.
// Values come from three layers: All Books (the settings file), then a language ("lang:es"), then one book
// ("book:12"). A layer holds only the values that differ; the library database keeps the language and book layers.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

struct Appearance {
    int textSizePct = 100;     // 100 % = 20 px body text
    int lineSpacingPct = 160;  // line height in % of the text size
    int marginPx = 56;         // the smallest space left and right of the text
    int pageWidthEm = 33;      // the longest line, in units of the text size
    QString columns = QStringLiteral("auto");   // auto / one / two
    bool justify = true;
    bool hyphenate = true;
    QString font;              // empty: the book's own fonts; otherwise a font family for all body text

    QJsonObject toJson() const;
    void merge(const QJsonObject& layer);   // applies the values a layer holds, keeping them in range
    void clamp();

    static QStringList keys();               // the JSON keys, for removing a value from narrower layers
    static QStringList bundledFonts();       // fonts that ship with Biblioglot
};

// Registers the bundled fonts with Qt (once).
void registerBundledFonts();

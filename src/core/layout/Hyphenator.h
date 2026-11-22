// Hyphenation with Liang's algorithm (as in TeX) and the hyph-utf8 patterns bundled in resources/hyphenation.
// The layout inserts soft hyphens (U+00AD) at the allowed breaks of the text it lays out; the book's text and its
// offsets never change (TextLines maps between the two).
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QVector>

#include <vector>

namespace layout {

class Hyphenator {
public:
    // "es", "es-ES", "en-GB" ...; null when no patterns ship for the language. Thread-safe; loaded once.
    static const Hyphenator* forLanguage(const QString& lang);

    // Allowed breaks inside a word: positions 1..size-1 (a break before word[i]).
    QList<int> breaks(QStringView word) const;

    // Inserts soft hyphens into a paragraph. toLayout[i] is the new index of text[i] (size text+1); toFlow[j] the
    // text index of new[j] (an inserted hyphen maps to the character after it; size new+1).
    QString hyphenate(const QString& text, std::vector<int>* toLayout, std::vector<int>* toFlow) const;

private:
    bool load(const QString& name, int leftMin, int rightMin);

    QHash<QString, QVector<quint8>> m_patterns;   // letters -> values between them
    QHash<QString, QList<int>> m_exceptions;      // whole words with fixed breaks
    int m_maxLen = 0;
    int m_leftMin = 2, m_rightMin = 2;
};

}  // namespace layout

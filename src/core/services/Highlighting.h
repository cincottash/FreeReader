// Highlighting a selection, as both the desktop and the touch app do it (Kindle's rules):
// - inside an existing highlight, the color changes; its own color again takes it away (unless it has a note);
// - a selection starting or ending inside a word takes the whole word;
// - highlights it overlaps are merged into the new one, keeping their notes.
#pragma once

#include <QString>

namespace core {

class Annotations;
class BookEngine;

struct HighlightChange {
    int id = 0;               // the highlight now there (0 when one was taken away, or on failure)
    bool removed = false;     // the same color again took a highlight away
    bool recolored = false;   // an existing highlight changed color
    int from = 0, to = 0;     // the new highlight's range (whole words, merged)
    QString text;             // its text
};

// A highlight as vocabulary for the Word List: its text without punctuation around it, when it is a word or a short
// phrase (up to 4 words); empty for a passage (a quote, not vocabulary) or for no letters.
QString vocabularyText(const QString& highlighted);
// The sentence for a Word List entry: a very long one (old books have them) cut to the part around the word.
QString contextAround(const QString& sentence, const QString& text);

// `chapter`: the chapter's title, kept with the highlight; `selectedText`: used when the chapter isn't laid out.
HighlightChange highlightRange(Annotations& notes, BookEngine& engine, int bookId, int spine, int start, int end,
                               const QString& color, const QString& chapter, const QString& selectedText);

}  // namespace core

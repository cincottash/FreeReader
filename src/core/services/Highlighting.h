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

// `chapter`: the chapter's title, kept with the highlight; `selectedText`: used when the chapter isn't laid out.
HighlightChange highlightRange(Annotations& notes, BookEngine& engine, int bookId, int spine, int start, int end,
                               const QString& color, const QString& chapter, const QString& selectedText);

}  // namespace core

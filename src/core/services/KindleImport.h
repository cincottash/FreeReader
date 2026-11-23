// Bringing Kindle highlights, notes and bookmarks into a book. Kindle exports (Bookcision's JSON, or a Kindle's
// "My Clippings.txt") give each highlight's text and its Kindle location, a position in Amazon's copy of the book
// that doesn't match an EPUB of it. So each highlight is found by its text; the highlights that occur only once in
// the book (anchors) tell how Kindle locations map to this book, and that picks the right one among repeated words.
// A highlight whose text isn't in this edition (another translation's wording) is reported, with the place it
// would be. Nothing is written here: the caller applies the plan.
#pragma once

#include "core/position/Position.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <atomic>
#include <functional>

namespace core {

class Book;

struct KindleItem {
    QString text;              // the highlighted text (empty for a bookmark)
    QString note;              // the note on it, if any
    int location = 0;          // Kindle location
    bool bookmark = false;     // a bookmark (a note-only entry, or a highlight whose note just says "bookmark")
};

struct KindleExport {
    QString title, authors;
    QList<KindleItem> items;
    QString error;             // empty when it could be read
};

// Reads a Bookcision JSON export or a "My Clippings.txt" (only the entries of the book whose title starts like
// `titleHint`, when given).
KindleExport parseKindleExport(const QByteArray& data, const QString& titleHint = QString());

struct KindlePlaced {
    // How it was placed: its own text, or (another edition's wording) the equivalent words between the highlights
    // around it: the same meaning in English, the same word in another form, or the word at the matching spot.
    enum Match { None, Exact, ByMeaning, ByForm, ByPosition };
    KindleItem item;
    bool found = false;        // the text is in this book, at [start, end) of chapter `spine`
    Match match = None;        // Exact when found; ByMeaning/ByForm/ByPosition: an equivalent at [start, end)
    QString equivalent;        // the book's words used instead
    int spine = -1, start = 0, end = 0;
    Position approx;           // where it would be (found or not)
    bool placed() const { return match != None; }
};

struct KindlePlan {
    QList<KindlePlaced> items;  // in the export's order
    int found = 0, notFound = 0, bookmarks = 0, notes = 0;
    int byMeaning = 0, byForm = 0, byPosition = 0;   // equivalents (when asked for)
    int anchors = 0;
    Position lastRead;          // the place of the furthest Kindle location
    int lastLocation = 0;
    double lastFraction = 0;    // 0..1 of the book
};

// `meanings` (optional) gives a word's English meanings (the offline dictionary), for equivalents by meaning.
// `equivalents`: place the highlights this edition words differently on their equivalent words.
using MeaningsOf = std::function<QStringList(const QString& word)>;
KindlePlan planKindleImport(const Book& book, const KindleExport& ex, const std::atomic<bool>* cancel = nullptr,
                            bool equivalents = false, const MeaningsOf& meanings = {});

}  // namespace core

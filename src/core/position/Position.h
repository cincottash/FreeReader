// Positions in a book: a spine item plus a character offset in its flow (fast, used while reading), and EPUB CFI
// strings (standard, saved to the database) that keep working when the font, window size or app version change.
#pragma once

#include <QString>

namespace layout {
struct ChapterFlow;
}

namespace core {

struct Position {
    int spine = 0;
    int offset = 0;
    bool operator==(const Position&) const = default;
    bool operator<(const Position& o) const { return spine != o.spine ? spine < o.spine : offset < o.offset; }
    bool operator<=(const Position& o) const { return !(o < *this); }
};

// "epubcfi(/6/8!/4/2[ch1]/10/1:42)" for an offset in spine item `spine`.
QString makeCfi(int spine, const layout::ChapterFlow& flow, int offset);
// Splits a CFI into its spine index, the path inside the chapter document and the character offset.
bool parseCfi(const QString& cfi, int* spine, QString* path, int* rawOffset);
// The flow offset a CFI path points to, or -1 if the chapter has changed too much to find it.
int resolveCfiPath(const layout::ChapterFlow& flow, const QString& path, int rawOffset);

}  // namespace core

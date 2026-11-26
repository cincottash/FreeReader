// Highlights, notes and bookmarks, per book, in the library database. Each keeps its place twice: the chapter
// offset (fast, for painting) and an EPUB CFI (survives changes to the app), plus the quoted text.
#pragma once

#include "core/position/Position.h"

#include <QColor>
#include <QDateTime>
#include <QList>
#include <QString>

namespace core {

class Library;
struct NotebookRow;

struct Annotation {
    enum Kind { Highlight = 0, Bookmark = 1 };
    int id = 0;
    int bookId = 0;
    Kind kind = Highlight;
    int spine = -1, start = 0, end = 0;   // a page bookmark: end == start; a word bookmark covers its word
    QString cfi;
    QString text;                          // the quoted text (a bookmark: the first words of the page)
    QString color;                         // highlight color key: pink, orange, yellow, blue
    QString note;
    QString pageLabel, chapter;
    QDateTime created, modified;
};

class Annotations {
public:
    explicit Annotations(Library* library);
    bool isOpen() const;

    static QStringList colors() { return {"pink", "orange", "yellow", "blue"}; }
    // The colors as the highlight dots show them: the most recently used first, the others in their order before.
    // `order` is what the settings kept (unknown keys are dropped, missing ones added in their usual place).
    static QStringList colorsInOrder(const QStringList& order);
    // `color` was just used: it moves to the front; the others keep their order.
    static QStringList usedColor(const QStringList& order, const QString& color);
    static QColor colorValue(const QString& key);   // the swatch color

    int add(const Annotation& a);                   // returns the id (0 on failure)
    bool update(const Annotation& a);               // color, note
    void remove(int id);
    Annotation get(int id) const;
    QList<Annotation> forBook(int bookId) const;    // in reading order
    // The bookmark added last, in any book (id 0 when there is none): Go to Latest Bookmark.
    Annotation latestBookmark() const;
    QList<Annotation> inChapter(int bookId, int spine) const;
    // Highlights overlapping [start, end) in a chapter.
    QList<Annotation> highlightsAt(int bookId, int spine, int start, int end) const;
    int bookmarkAt(int bookId, int spine, int from, int to) const;   // the first page bookmark in [from, to], or 0
    // The word bookmark overlapping [start, end) of a chapter, or 0.
    int wordBookmarkAt(int bookId, int spine, int start, int end) const;
    static bool isWordBookmark(const Annotation& a) { return a.kind == Annotation::Bookmark && a.end > a.start; }
    int count(int bookId) const;

    // Markdown: the book's title, then each highlight as a quote with its note and page, and the bookmarks.
    QString markdown(int bookId, const QString& title, const QString& author) const;
    // JSON for other tools: {"book": {...}, "annotations": [{kind, color, text, note, page, chapter, cfi, created}]}.
    QByteArray json(int bookId, const QString& title, const QString& author, const QString& path) const;
    // Spreadsheets and plain text (the columns kind, color, text, note, page, chapter, cfi, created, book). Every
    // format here can be read back by File > Import Highlights (core/services/KindleImport.h).
    QString csv(int bookId, const QString& title) const;
    QString tsv(int bookId, const QString& title) const;
    QString plainText(int bookId, const QString& title, const QString& author) const;

private:
    QList<NotebookRow> rows(int bookId) const;
    QString m_connection;
};

}  // namespace core

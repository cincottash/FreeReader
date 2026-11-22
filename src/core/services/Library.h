// The library database (SQLite, one file in the app data folder): the books the reader has opened and the
// reading position of each. Phase 2 adds collections and the library view; phase 4 annotations and the word log.
#pragma once

#include "core/position/Position.h"

#include <QDateTime>
#include <QList>
#include <QString>

namespace core {

struct BookMetadata;

struct LibraryBook {
    int id = 0;
    QString path;
    QString title;
    QString author;
    QString language;
    double progress = 0;     // 0..1
    QDateTime lastOpened;
};

struct SavedPosition {
    Position position;
    QString cfi;
    double progress = 0;
};

class Library {
public:
    ~Library();
    bool open(const QString& databaseFile, QString* error);
    bool isOpen() const { return m_open; }

    // Adds the book or updates its details; returns its id (0 on failure).
    int touchBook(const QString& path, const BookMetadata& meta);
    void savePosition(int bookId, const SavedPosition& pos);
    bool loadPosition(int bookId, SavedPosition* pos) const;
    QList<LibraryBook> recentBooks(int limit) const;

private:
    QString m_connection;
    bool m_open = false;
};

}  // namespace core

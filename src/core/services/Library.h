// The library database (SQLite, one file in the app data folder): the books (their files stay where they are),
// reading positions, cover thumbnails, collections, and appearance layers. Phase 4 adds annotations and the
// word log.
#pragma once

#include "core/position/Position.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

namespace core {

struct BookMetadata;

struct LibraryBook {
    int id = 0;
    QString path;
    QString title;
    QString author;
    QString language;
    double progress = 0;       // 0..1
    QDateTime added;
    QDateTime lastOpened;      // invalid: never opened
    bool markedFinished = false;
    QString pageLabel, pageCount;   // "412", "1,180" when last read
    bool hasCover = false;

    bool finished() const { return markedFinished || progress >= 0.995; }
    bool started() const { return progress > 0.0005 || lastOpened.isValid(); }
};

struct Collection {
    int id = 0;
    QString name;
    int count = 0;
};

struct SavedPosition {
    Position position;
    QString cfi;
    double progress = 0;
    QString pageLabel, pageCount;
};

class Library {
public:
    ~Library();
    bool open(const QString& databaseFile, QString* error);
    bool isOpen() const { return m_open; }

    // Opening a book: adds it or updates its details, and records that it was opened. Returns its id (0 on failure).
    int touchBook(const QString& path, const BookMetadata& meta);
    // Adding a book to the library (import): adds it or updates its details, keeps the cover thumbnail (PNG).
    int addBook(const QString& path, const BookMetadata& meta, const QByteArray& coverPng, const QByteArray& contentHash = {});
    // A book already in the library that the file at `path` is a copy of: the same bytes (fingerprint), or the same
    // identifier, title and author. 0 when it's new (or when it's that same path: adding it again updates it).
    int duplicateOf(const QString& path, const BookMetadata& meta, const QByteArray& contentHash);
    QSet<QString> bookPaths() const;   // every book's file, and the copies already found (canonical paths)
    void rememberDuplicate(const QString& path, int bookId);   // a folder check reads it no more
    QList<LibraryBook> books() const;
    QList<LibraryBook> recentBooks(int limit) const;   // opened books, most recent first
    LibraryBook book(int id) const;
    int bookIdForPath(const QString& path) const;
    QByteArray cover(int bookId) const;
    bool removeBook(int bookId);                        // from the library only; the file is never touched
    bool setPath(int bookId, const QString& path);      // after the file was moved (Locate…)
    void setFinished(int bookId, bool finished);

    void savePosition(int bookId, const SavedPosition& pos);
    bool loadPosition(int bookId, SavedPosition* pos) const;

    QList<Collection> collections() const;              // by name
    int createCollection(const QString& name);          // 0 if the name is taken or empty
    bool renameCollection(int id, const QString& name);
    void deleteCollection(int id);                      // the books stay in the library
    void addToCollection(int collectionId, int bookId);
    void removeFromCollection(int collectionId, int bookId);
    QHash<int, QSet<int>> collectionMembers() const;    // collection id -> book ids

    // Appearance layers: "lang:<code>" or "book:<id>"; an empty layer removes the row.
    QJsonObject appearanceLayer(const QString& scope) const;
    void setAppearanceLayer(const QString& scope, const QJsonObject& layer);

    // Translation cache: a lookup's answer (TranslationResult JSON) by its key (TranslationRequest::cacheKey).
    // `provider`: which engine gave it (TranslationProvider::name()).
    QJsonObject cachedTranslation(const QString& key, QString* provider = nullptr) const;
    void cacheTranslation(const QString& key, const QString& provider, const QJsonObject& result);
    int cachedTranslationCount() const;

    static QString canonicalPath(const QString& path);
    QString connectionName() const { return m_connection; }   // for the word log, which shares the database

private:
    QString m_connection;
    bool m_open = false;
};

}  // namespace core

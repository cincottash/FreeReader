#include "Library.h"

#include "core/format/Book.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace core {

namespace {

constexpr int kSchemaVersion = 7;

QString nowIso() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

const char* kBookColumns =
    "SELECT b.id, b.path, b.title, b.author, b.language, b.added, b.last_opened, b.finished, "
    "COALESCE(p.progress, 0), COALESCE(p.page_label, ''), COALESCE(p.page_count, ''), (c.book_id IS NOT NULL) "
    "FROM books b LEFT JOIN positions p ON p.book_id = b.id LEFT JOIN covers c ON c.book_id = b.id ";

LibraryBook readBook(const QSqlQuery& q)
{
    LibraryBook b;
    b.id = q.value(0).toInt();
    b.path = q.value(1).toString();
    b.title = q.value(2).toString();
    b.author = q.value(3).toString();
    b.language = q.value(4).toString();
    b.added = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
    if (!q.value(6).isNull() && !q.value(6).toString().isEmpty()) b.lastOpened = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
    b.markedFinished = q.value(7).toInt() != 0;
    b.progress = q.value(8).toDouble();
    b.pageLabel = q.value(9).toString();
    b.pageCount = q.value(10).toString();
    b.hasCover = q.value(11).toBool();
    return b;
}

}  // namespace

QString Library::canonicalPath(const QString& path)
{
    const QFileInfo fi(path);
    const QString c = fi.canonicalFilePath();
    return QDir::cleanPath(c.isEmpty() ? fi.absoluteFilePath() : c);
}

Library::~Library()
{
    if (!m_connection.isEmpty()) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connection, false);
            if (db.isOpen()) db.close();
        }
        QSqlDatabase::removeDatabase(m_connection);
    }
}

bool Library::open(const QString& databaseFile, QString* error)
{
    m_connection = QStringLiteral("library-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(databaseFile);
    if (!db.open()) {
        if (error) *error = db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    q.exec(QStringLiteral("PRAGMA user_version"));
    const int version = q.next() ? q.value(0).toInt() : 0;
    auto run = [&](const char* sql) {
        if (q.exec(QString::fromLatin1(sql))) return true;
        if (error) *error = q.lastError().text();
        return false;
    };
    if (version < 1) {
        if (!run("CREATE TABLE IF NOT EXISTS books ("
                 " id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, identifier TEXT, title TEXT, author TEXT, language TEXT,"
                 " added TEXT NOT NULL, last_opened TEXT)"))
            return false;
        if (!run("CREATE TABLE IF NOT EXISTS positions ("
                 " book_id INTEGER PRIMARY KEY REFERENCES books(id) ON DELETE CASCADE, spine INTEGER NOT NULL, offset INTEGER NOT NULL,"
                 " cfi TEXT, progress REAL, updated TEXT NOT NULL)"))
            return false;
    }
    if (version < 2) {
        // Appearance for one language ("lang:es") or one book ("book:12"): only the values that differ.
        if (!run("CREATE TABLE IF NOT EXISTS appearance_layers (scope TEXT PRIMARY KEY, json TEXT NOT NULL)")) return false;
    }
    if (version < 3) {
        if (!run("ALTER TABLE books ADD COLUMN finished INTEGER NOT NULL DEFAULT 0")) return false;
        if (!run("ALTER TABLE positions ADD COLUMN page_label TEXT")) return false;
        if (!run("ALTER TABLE positions ADD COLUMN page_count TEXT")) return false;
        if (!run("CREATE TABLE IF NOT EXISTS covers (book_id INTEGER PRIMARY KEY REFERENCES books(id) ON DELETE CASCADE, png BLOB NOT NULL)"))
            return false;
        if (!run("CREATE TABLE IF NOT EXISTS collections (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE, created TEXT NOT NULL)"))
            return false;
        if (!run("CREATE TABLE IF NOT EXISTS collection_books ("
                 " collection_id INTEGER NOT NULL REFERENCES collections(id) ON DELETE CASCADE,"
                 " book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE, PRIMARY KEY (collection_id, book_id))"))
            return false;
    }
    if (version < 4) {
        // Translations already looked up (any provider), so the same lookup never uses an allowance twice.
        if (!run("CREATE TABLE IF NOT EXISTS translation_cache (key TEXT PRIMARY KEY, provider TEXT NOT NULL, json TEXT NOT NULL,"
                 " created TEXT NOT NULL)"))
            return false;
    }
    if (version < 5) {
        // The word log: every word or passage looked up, one row per word (by its dictionary form), with the last
        // place it was seen. Practice cards: one per word and direction, with their spaced-repetition state.
        if (!run("CREATE TABLE IF NOT EXISTS words ("
                 " id INTEGER PRIMARY KEY, language TEXT NOT NULL, word_key TEXT NOT NULL, text TEXT NOT NULL, lemma TEXT NOT NULL DEFAULT '',"
                 " passage INTEGER NOT NULL DEFAULT 0, translation TEXT NOT NULL DEFAULT '', alternatives TEXT NOT NULL DEFAULT '[]',"
                 " part_of_speech TEXT NOT NULL DEFAULT '', definition TEXT NOT NULL DEFAULT '', sentence TEXT NOT NULL DEFAULT '',"
                 " sentence_translation TEXT NOT NULL DEFAULT '', note TEXT NOT NULL DEFAULT '', user_note TEXT NOT NULL DEFAULT '',"
                 " edited INTEGER NOT NULL DEFAULT 0, starred INTEGER NOT NULL DEFAULT 0,"
                 " book_id INTEGER REFERENCES books(id) ON DELETE SET NULL, book_title TEXT NOT NULL DEFAULT '',"
                 " spine INTEGER NOT NULL DEFAULT -1, sel_start INTEGER NOT NULL DEFAULT 0, sel_end INTEGER NOT NULL DEFAULT 0,"
                 " cfi TEXT NOT NULL DEFAULT '', page_label TEXT NOT NULL DEFAULT '', chapter TEXT NOT NULL DEFAULT '',"
                 " lookups INTEGER NOT NULL DEFAULT 1, added TEXT NOT NULL, last_seen TEXT NOT NULL, UNIQUE(language, word_key))"))
            return false;
        if (!run("CREATE TABLE IF NOT EXISTS cards ("
                 " id INTEGER PRIMARY KEY, word_id INTEGER NOT NULL REFERENCES words(id) ON DELETE CASCADE, direction TEXT NOT NULL,"
                 " state INTEGER NOT NULL DEFAULT 0, step INTEGER NOT NULL DEFAULT 0, due TEXT NOT NULL, stability REAL NOT NULL DEFAULT 0,"
                 " difficulty REAL NOT NULL DEFAULT 0, reps INTEGER NOT NULL DEFAULT 0, lapses INTEGER NOT NULL DEFAULT 0,"
                 " last_review TEXT NOT NULL DEFAULT '', suspended INTEGER NOT NULL DEFAULT 0, UNIQUE(word_id, direction))"))
            return false;
        if (!run("CREATE TABLE IF NOT EXISTS reviews ("
                 " id INTEGER PRIMARY KEY, card_id INTEGER NOT NULL REFERENCES cards(id) ON DELETE CASCADE, rating INTEGER NOT NULL,"
                 " state INTEGER NOT NULL, reviewed TEXT NOT NULL, elapsed_days REAL NOT NULL DEFAULT 0)"))
            return false;
    }
    if (version < 6) {
        // Highlights (with a color and an optional note) and bookmarks.
        if (!run("CREATE TABLE IF NOT EXISTS annotations ("
                 " id INTEGER PRIMARY KEY, book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE, kind INTEGER NOT NULL,"
                 " spine INTEGER NOT NULL, sel_start INTEGER NOT NULL, sel_end INTEGER NOT NULL, cfi TEXT NOT NULL DEFAULT '',"
                 " text TEXT NOT NULL DEFAULT '', color TEXT NOT NULL DEFAULT '', note TEXT NOT NULL DEFAULT '',"
                 " page_label TEXT NOT NULL DEFAULT '', chapter TEXT NOT NULL DEFAULT '', created TEXT NOT NULL, modified TEXT NOT NULL)"))
            return false;
        if (!run("CREATE INDEX IF NOT EXISTS annotations_book ON annotations(book_id, spine, sel_start)")) return false;
    }
    if (version < 7) {
        // Reading time for the statistics: per book and local day, seconds read (gaps capped), pages turned forward
        // and characters read.
        if (!run("CREATE TABLE IF NOT EXISTS reading_log (book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,"
                 " day TEXT NOT NULL, seconds INTEGER NOT NULL DEFAULT 0, pages INTEGER NOT NULL DEFAULT 0, chars INTEGER NOT NULL DEFAULT 0,"
                 " PRIMARY KEY (book_id, day))"))
            return false;
    }
    if (version < kSchemaVersion) q.exec(QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion));
    m_open = true;
    return true;
}

int Library::touchBook(const QString& path, const BookMetadata& meta)
{
    const int id = addBook(path, meta, QByteArray());
    if (!id) return 0;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("UPDATE books SET last_opened=? WHERE id=?"));
    q.addBindValue(nowIso());
    q.addBindValue(id);
    q.exec();
    return id;
}

int Library::addBook(const QString& path, const BookMetadata& meta, const QByteArray& coverPng)
{
    if (!m_open) return 0;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    const QString p = canonicalPath(path);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO books (path, identifier, title, author, language, added) VALUES (?,?,?,?,?,?) "
                             "ON CONFLICT(path) DO UPDATE SET identifier=excluded.identifier, title=excluded.title, "
                             "author=excluded.author, language=excluded.language"));
    q.addBindValue(p);
    q.addBindValue(meta.identifier);
    q.addBindValue(meta.title);
    q.addBindValue(meta.authors.join(QStringLiteral(", ")));
    q.addBindValue(meta.language);
    q.addBindValue(nowIso());
    if (!q.exec()) return 0;
    const int id = bookIdForPath(p);
    if (id && !coverPng.isEmpty()) {
        QSqlQuery c(db);
        c.prepare(QStringLiteral("INSERT INTO covers (book_id, png) VALUES (?,?) ON CONFLICT(book_id) DO UPDATE SET png=excluded.png"));
        c.addBindValue(id);
        c.addBindValue(coverPng);
        c.exec();
    }
    return id;
}

int Library::bookIdForPath(const QString& path) const
{
    if (!m_open) return 0;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("SELECT id FROM books WHERE path=?"));
    q.addBindValue(canonicalPath(path));
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

QList<LibraryBook> Library::books() const
{
    QList<LibraryBook> out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (!q.exec(QString::fromLatin1(kBookColumns))) return out;
    while (q.next()) out << readBook(q);
    return out;
}

QList<LibraryBook> Library::recentBooks(int limit) const
{
    QList<LibraryBook> out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QString::fromLatin1(kBookColumns) + QStringLiteral("WHERE b.last_opened IS NOT NULL ORDER BY b.last_opened DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec()) return out;
    while (q.next()) out << readBook(q);
    return out;
}

LibraryBook Library::book(int id) const
{
    if (!m_open) return {};
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QString::fromLatin1(kBookColumns) + QStringLiteral("WHERE b.id=?"));
    q.addBindValue(id);
    return q.exec() && q.next() ? readBook(q) : LibraryBook();
}

QByteArray Library::cover(int bookId) const
{
    if (!m_open) return {};
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("SELECT png FROM covers WHERE book_id=?"));
    q.addBindValue(bookId);
    return q.exec() && q.next() ? q.value(0).toByteArray() : QByteArray();
}

bool Library::removeBook(int bookId)
{
    if (!m_open) return false;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("DELETE FROM books WHERE id=?"));
    q.addBindValue(bookId);
    const bool ok = q.exec();
    setAppearanceLayer(QStringLiteral("book:%1").arg(bookId), {});
    return ok;
}

bool Library::setPath(int bookId, const QString& path)
{
    if (!m_open) return false;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("UPDATE books SET path=? WHERE id=?"));
    q.addBindValue(canonicalPath(path));
    q.addBindValue(bookId);
    return q.exec() && q.numRowsAffected() == 1;
}

void Library::setFinished(int bookId, bool finished)
{
    if (!m_open) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("UPDATE books SET finished=? WHERE id=?"));
    q.addBindValue(finished ? 1 : 0);
    q.addBindValue(bookId);
    q.exec();
}

void Library::savePosition(int bookId, const SavedPosition& pos)
{
    if (!m_open || bookId <= 0) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT INTO positions (book_id, spine, offset, cfi, progress, updated, page_label, page_count) VALUES (?,?,?,?,?,?,?,?) "
                             "ON CONFLICT(book_id) DO UPDATE SET spine=excluded.spine, offset=excluded.offset, cfi=excluded.cfi, "
                             "progress=excluded.progress, updated=excluded.updated, page_label=excluded.page_label, page_count=excluded.page_count"));
    q.addBindValue(bookId);
    q.addBindValue(pos.position.spine);
    q.addBindValue(pos.position.offset);
    q.addBindValue(pos.cfi);
    q.addBindValue(pos.progress);
    q.addBindValue(nowIso());
    q.addBindValue(pos.pageLabel);
    q.addBindValue(pos.pageCount);
    q.exec();
}

bool Library::loadPosition(int bookId, SavedPosition* pos) const
{
    if (!m_open || bookId <= 0) return false;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("SELECT spine, offset, cfi, progress, COALESCE(page_label, ''), COALESCE(page_count, '') FROM positions WHERE book_id=?"));
    q.addBindValue(bookId);
    if (!q.exec() || !q.next()) return false;
    pos->position = {q.value(0).toInt(), q.value(1).toInt()};
    pos->cfi = q.value(2).toString();
    pos->progress = q.value(3).toDouble();
    pos->pageLabel = q.value(4).toString();
    pos->pageCount = q.value(5).toString();
    return true;
}

QList<Collection> Library::collections() const
{
    QList<Collection> out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (!q.exec(QStringLiteral("SELECT c.id, c.name, COUNT(cb.book_id) FROM collections c LEFT JOIN collection_books cb ON cb.collection_id = c.id "
                               "GROUP BY c.id ORDER BY c.name COLLATE NOCASE")))
        return out;
    while (q.next()) out << Collection{q.value(0).toInt(), q.value(1).toString(), q.value(2).toInt()};
    return out;
}

int Library::createCollection(const QString& name)
{
    const QString n = name.simplified();
    if (!m_open || n.isEmpty()) return 0;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT INTO collections (name, created) VALUES (?,?)"));
    q.addBindValue(n);
    q.addBindValue(nowIso());
    if (!q.exec()) return 0;
    return q.lastInsertId().toInt();
}

bool Library::renameCollection(int id, const QString& name)
{
    const QString n = name.simplified();
    if (!m_open || n.isEmpty()) return false;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("UPDATE collections SET name=? WHERE id=?"));
    q.addBindValue(n);
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

void Library::deleteCollection(int id)
{
    if (!m_open) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("DELETE FROM collections WHERE id=?"));
    q.addBindValue(id);
    q.exec();
}

void Library::addToCollection(int collectionId, int bookId)
{
    if (!m_open) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO collection_books (collection_id, book_id) VALUES (?,?)"));
    q.addBindValue(collectionId);
    q.addBindValue(bookId);
    q.exec();
}

void Library::removeFromCollection(int collectionId, int bookId)
{
    if (!m_open) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("DELETE FROM collection_books WHERE collection_id=? AND book_id=?"));
    q.addBindValue(collectionId);
    q.addBindValue(bookId);
    q.exec();
}

QHash<int, QSet<int>> Library::collectionMembers() const
{
    QHash<int, QSet<int>> out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (!q.exec(QStringLiteral("SELECT collection_id, book_id FROM collection_books"))) return out;
    while (q.next()) out[q.value(0).toInt()].insert(q.value(1).toInt());
    return out;
}

QJsonObject Library::appearanceLayer(const QString& scope) const
{
    if (!m_open) return {};
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("SELECT json FROM appearance_layers WHERE scope=?"));
    q.addBindValue(scope);
    if (!q.exec() || !q.next()) return {};
    return QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
}

QJsonObject Library::cachedTranslation(const QString& key, QString* provider) const
{
    if (!m_open) return {};
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("SELECT json, provider FROM translation_cache WHERE key=?"));
    q.addBindValue(key);
    if (!q.exec() || !q.next()) return {};
    if (provider) *provider = q.value(1).toString();
    return QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
}

void Library::cacheTranslation(const QString& key, const QString& provider, const QJsonObject& result)
{
    if (!m_open) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT INTO translation_cache (key, provider, json, created) VALUES (?,?,?,?)"
                             " ON CONFLICT(key) DO UPDATE SET provider=excluded.provider, json=excluded.json, created=excluded.created"));
    q.addBindValue(key);
    q.addBindValue(provider);
    q.addBindValue(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.exec();
}

int Library::cachedTranslationCount() const
{
    if (!m_open) return 0;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    return q.exec(QStringLiteral("SELECT COUNT(*) FROM translation_cache")) && q.next() ? q.value(0).toInt() : 0;
}

void Library::setAppearanceLayer(const QString& scope, const QJsonObject& layer)
{
    if (!m_open) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (layer.isEmpty()) {
        q.prepare(QStringLiteral("DELETE FROM appearance_layers WHERE scope=?"));
        q.addBindValue(scope);
    } else {
        q.prepare(QStringLiteral("INSERT INTO appearance_layers (scope, json) VALUES (?,?) ON CONFLICT(scope) DO UPDATE SET json=excluded.json"));
        q.addBindValue(scope);
        q.addBindValue(QString::fromUtf8(QJsonDocument(layer).toJson(QJsonDocument::Compact)));
    }
    q.exec();
}

}  // namespace core

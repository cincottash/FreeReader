#include "Library.h"

#include "core/format/Book.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace core {

namespace {

constexpr int kSchemaVersion = 1;

QString canonical(const QString& path)
{
    const QFileInfo fi(path);
    const QString c = fi.canonicalFilePath();
    return QDir::cleanPath(c.isEmpty() ? fi.absoluteFilePath() : c);
}

}  // namespace

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
    if (version < 1) {
        const char* schema[] = {
            "CREATE TABLE IF NOT EXISTS books ("
            " id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, identifier TEXT, title TEXT, author TEXT, language TEXT,"
            " added TEXT NOT NULL, last_opened TEXT)",
            "CREATE TABLE IF NOT EXISTS positions ("
            " book_id INTEGER PRIMARY KEY REFERENCES books(id) ON DELETE CASCADE, spine INTEGER NOT NULL, offset INTEGER NOT NULL,"
            " cfi TEXT, progress REAL, updated TEXT NOT NULL)",
        };
        for (const char* s : schema) {
            if (!q.exec(QString::fromLatin1(s))) {
                if (error) *error = q.lastError().text();
                return false;
            }
        }
        q.exec(QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion));
    }
    m_open = true;
    return true;
}

int Library::touchBook(const QString& path, const BookMetadata& meta)
{
    if (!m_open) return 0;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    const QString p = canonical(path);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO books (path, identifier, title, author, language, added, last_opened) VALUES (?,?,?,?,?,?,?) "
                             "ON CONFLICT(path) DO UPDATE SET identifier=excluded.identifier, title=excluded.title, "
                             "author=excluded.author, language=excluded.language, last_opened=excluded.last_opened"));
    q.addBindValue(p);
    q.addBindValue(meta.identifier);
    q.addBindValue(meta.title);
    q.addBindValue(meta.authors.join(QStringLiteral(", ")));
    q.addBindValue(meta.language);
    q.addBindValue(now);
    q.addBindValue(now);
    if (!q.exec()) return 0;
    QSqlQuery id(db);
    id.prepare(QStringLiteral("SELECT id FROM books WHERE path=?"));
    id.addBindValue(p);
    return id.exec() && id.next() ? id.value(0).toInt() : 0;
}

void Library::savePosition(int bookId, const SavedPosition& pos)
{
    if (!m_open || bookId <= 0) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT INTO positions (book_id, spine, offset, cfi, progress, updated) VALUES (?,?,?,?,?,?) "
                             "ON CONFLICT(book_id) DO UPDATE SET spine=excluded.spine, offset=excluded.offset, cfi=excluded.cfi, "
                             "progress=excluded.progress, updated=excluded.updated"));
    q.addBindValue(bookId);
    q.addBindValue(pos.position.spine);
    q.addBindValue(pos.position.offset);
    q.addBindValue(pos.cfi);
    q.addBindValue(pos.progress);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.exec();
}

bool Library::loadPosition(int bookId, SavedPosition* pos) const
{
    if (!m_open || bookId <= 0) return false;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("SELECT spine, offset, cfi, progress FROM positions WHERE book_id=?"));
    q.addBindValue(bookId);
    if (!q.exec() || !q.next()) return false;
    pos->position = {q.value(0).toInt(), q.value(1).toInt()};
    pos->cfi = q.value(2).toString();
    pos->progress = q.value(3).toDouble();
    return true;
}

QList<LibraryBook> Library::recentBooks(int limit) const
{
    QList<LibraryBook> out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("SELECT b.id, b.path, b.title, b.author, b.language, b.last_opened, COALESCE(p.progress, 0) "
                             "FROM books b LEFT JOIN positions p ON p.book_id = b.id ORDER BY b.last_opened DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec()) return out;
    while (q.next()) {
        LibraryBook b;
        b.id = q.value(0).toInt();
        b.path = q.value(1).toString();
        b.title = q.value(2).toString();
        b.author = q.value(3).toString();
        b.language = q.value(4).toString();
        b.lastOpened = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
        b.progress = q.value(6).toDouble();
        out << b;
    }
    return out;
}

}  // namespace core

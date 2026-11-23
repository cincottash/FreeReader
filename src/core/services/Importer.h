// Adding books to the library: reads each file's details and cover on a background thread (folders are searched
// for books of the supported formats). The files are never copied or moved; the library keeps their paths.
#pragma once

#include "core/format/Book.h"

#include <QObject>
#include <QSet>
#include <QStringList>
#include <QThreadPool>

#include <atomic>

namespace core {

struct ImportedBook {
    QString path;
    BookMetadata meta;
    QByteArray coverPng;   // a thumbnail, empty when the book has no cover
    QByteArray contentHash;   // SHA-256 of the file (hex): the same book under another name or folder is a duplicate
};

QByteArray fileHash(const QString& path);   // SHA-256 of a file's bytes (hex), empty when it can't be read

class Importer : public QObject {
    Q_OBJECT
public:
    explicit Importer(QObject* parent = nullptr);
    ~Importer() override;

    void add(const QStringList& filesOrFolders);
    // `skip`: files (canonical paths) not to read again, the books already in the library (a folder check).
    void add(const QStringList& filesOrFolders, const QSet<QString>& skip);
    bool isBusy() const { return m_running > 0; }

    static QByteArray thumbnail(const Book& book, int height = 360);

signals:
    void progress(int done, int total, const QString& name);
    void bookRead(const core::ImportedBook& book);
    void failed(const QString& path, const QString& message);
    void finished(int read, int failed);

private:
    QThreadPool m_pool;
    std::atomic<bool> m_stop{false};
    int m_running = 0;
};

}  // namespace core

Q_DECLARE_METATYPE(core::ImportedBook)

#include "Importer.h"
#include <algorithm>
#include <QFile>
#include <QCryptographicHash>

#include "core/format/FormatPlugin.h"

#include <QBuffer>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

namespace core {

Importer::Importer(QObject* parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);   // one book at a time, in order; the reader's own layout has the other threads
}

Importer::~Importer()
{
    m_stop = true;
    m_pool.waitForDone();
}

QByteArray Importer::thumbnail(const Book& book, int height)
{
    if (book.metadata.coverPath.isEmpty()) return {};
    bool ok = false;
    const QByteArray data = book.resource(book.metadata.coverPath, &ok);
    if (!ok) return {};
    QBuffer in;
    in.setData(data);
    in.open(QIODevice::ReadOnly);
    QImageReader r(&in);
    r.setAutoTransform(true);
    const QSize s = r.size();
    if (s.isValid() && s.height() > height) r.setScaledSize(s.scaled(QSize(height * 4, height), Qt::KeepAspectRatio));
    const QImage img = r.read();
    if (img.isNull()) return {};
    QByteArray png;
    QBuffer out(&png);
    out.open(QIODevice::WriteOnly);
    img.save(&out, "PNG");
    return png;
}

QByteArray fileHash(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return {};
    return h.result().toHex();
}

void Importer::add(const QStringList& filesOrFolders) { add(filesOrFolders, {}); }

void Importer::add(const QStringList& filesOrFolders, const QSet<QString>& skip)
{
    ++m_running;
    m_stop = false;
    m_pool.start([this, filesOrFolders, skip] {
        // Expand folders into the book files inside them.
        const QStringList suffixes = FormatRegistry::instance().fileSuffixes();
        QStringList patterns;
        for (const QString& s : suffixes) patterns << QStringLiteral("*.") + s;
        QStringList files;
        for (const QString& p : filesOrFolders) {
            const QFileInfo fi(p);
            if (fi.isDir()) {
                QDirIterator it(p, patterns, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
                while (it.hasNext() && !m_stop) files << it.next();
            } else {
                files << p;
            }
        }
        if (!skip.isEmpty())
            files.erase(std::remove_if(files.begin(), files.end(), [&](const QString& f) { return skip.contains(QFileInfo(f).canonicalFilePath()); }),
                        files.end());
        files.sort(Qt::CaseInsensitive);
        int done = 0, bad = 0;
        for (const QString& f : files) {
            if (m_stop) break;
            QMetaObject::invokeMethod(this, [this, done, n = int(files.size()), name = QFileInfo(f).fileName()] { emit progress(done, n, name); },
                                      Qt::QueuedConnection);
            QString error;
            std::shared_ptr<Book> book = FormatRegistry::instance().open(f, &error);
            if (!book) {
                ++bad;
                QMetaObject::invokeMethod(this, [this, f, error] { emit failed(f, error); }, Qt::QueuedConnection);
            } else {
                ImportedBook ib{f, book->metadata, thumbnail(*book), fileHash(f)};
                QMetaObject::invokeMethod(this, [this, ib] { emit bookRead(ib); }, Qt::QueuedConnection);
            }
            ++done;
        }
        QMetaObject::invokeMethod(this, [this, done, bad, n = int(files.size())] {
            emit progress(n, n, QString());
            --m_running;
            emit finished(done - bad, bad);
        }, Qt::QueuedConnection);
    });
}

}  // namespace core

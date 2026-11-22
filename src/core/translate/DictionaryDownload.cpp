#include "DictionaryDownload.h"

#include "core/zip/ZipArchive.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>

namespace core {

DictionaryDownload::Source DictionaryDownload::spanishEnglish()
{
    Source s;
    s.url = QUrl(QStringLiteral("https://github.com/cincottash/FreeReader/releases/download/dictionary-es-en-1/FreeReader-dictionary-es-en.zip"));
    s.sha256 = "35f0ac5f35bf8fb2cd7c6ad11ea4e0375e3a47fe979e1e686387888fdf0830d8";
    s.size = 30644037;
    s.entry = QStringLiteral("es-en.sqlite");
    s.unpackedSize = 109469696;
    return s;
}

DictionaryDownload::DictionaryDownload(QObject* parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

DictionaryDownload::~DictionaryDownload()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
    cleanUp();
}

void DictionaryDownload::cleanUp()
{
    if (m_part.isOpen()) m_part.close();
    if (!m_part.fileName().isEmpty()) QFile::remove(m_part.fileName());
}

void DictionaryDownload::fail(const QString& error)
{
    cleanUp();
    m_running = false;
    emit finished(m_out, error);
}

void DictionaryDownload::cancel()
{
    if (!m_running) return;
    m_canceled = true;
    if (m_reply) m_reply->abort();   // finished() follows, which reports it
}

void DictionaryDownload::start(const Source& source, const QString& outFile)
{
    if (m_running) return;
    m_source = source;
    m_out = outFile;
    m_received = 0;
    m_canceled = false;
    m_hash.reset();
    m_running = true;
    const QString dir = QFileInfo(outFile).absolutePath();
    if (!QDir().mkpath(dir)) return fail(tr("The app data folder can't be written to."));
    // Room for the zip and the unpacked file, with a margin.
    const QStorageInfo disk(dir);
    const qint64 needed = source.size + source.unpackedSize + 20 * 1024 * 1024;
    if (disk.isValid() && disk.bytesAvailable() >= 0 && disk.bytesAvailable() < needed)
        return fail(tr("Not enough free space: the dictionary needs about %1 MB.").arg((needed + 999999) / 1000000));
    m_part.setFileName(outFile + QStringLiteral(".zip.part"));
    if (!m_part.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail(tr("The download can't be saved: %1").arg(m_part.errorString()));

    QNetworkRequest req(source.url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FreeReader"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(60000);   // no data for a minute: give up
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray chunk = m_reply->readAll();
        m_received += chunk.size();
        // Far more than expected: not the dictionary.
        if (m_source.size > 0 && m_received > m_source.size + 1024 * 1024) {
            m_reply->abort();
            return;
        }
        m_hash.addData(chunk);
        if (m_part.write(chunk) != chunk.size()) m_reply->abort();
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        emit progress(got, total > 0 ? total : m_source.size);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* r = m_reply;
        r->deleteLater();
        m_reply = nullptr;
        if (m_canceled) return fail(tr("Canceled."));
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool writeFailed = m_part.error() != QFileDevice::NoError;
        if (writeFailed) return fail(tr("The download can't be saved: %1").arg(m_part.errorString()));
        if (r->error() != QNetworkReply::NoError && status == 0)
            return fail(tr("The download failed (%1). Check the internet connection and try again.").arg(r->errorString()));
        if (status != 200) return fail(tr("The dictionary isn't available on the server right now (HTTP %1).").arg(status));
        if (r->error() != QNetworkReply::NoError || m_received != m_source.size || m_hash.result().toHex() != m_source.sha256)
            return fail(tr("The downloaded file is damaged or not the expected one, so it wasn't installed. Try again."));
        m_part.close();
        // Unpacking 100 MB takes a moment: off the UI thread.
        const QString zip = m_part.fileName(), out = m_out, entry = m_source.entry;
        auto* watch = new QFutureWatcher<QString>(this);
        connect(watch, &QFutureWatcher<QString>::finished, this, [this, watch] {
            const QString err = watch->result();
            watch->deleteLater();
            if (!err.isEmpty()) return fail(err);
            cleanUp();
            m_running = false;
            emit finished(m_out, QString());
        });
        watch->setFuture(QtConcurrent::run([zip, out, entry]() -> QString {
            ZipArchive archive;
            QString why;
            if (!archive.open(zip, &why)) return QObject::tr("The download can't be unpacked: %1").arg(why);
            bool ok = false;
            const QByteArray data = archive.read(entry, &ok);
            if (!ok || data.isEmpty()) return QObject::tr("The download doesn't contain the dictionary.");
            QSaveFile f(out);
            if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
                return QObject::tr("The dictionary can't be saved: %1").arg(f.errorString());
            return {};
        }));
    });
}

}  // namespace core

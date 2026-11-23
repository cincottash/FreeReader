// Downloads the offline dictionary from FreeReader's GitHub releases, only when the user asks: a zip with the
// SQLite file and its license. The zip is checked against the size and SHA-256 built into the app before it is
// unpacked, so a damaged or replaced file is never installed. The caller installs the unpacked file.
#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace core {

class DictionaryDownload : public QObject {
    Q_OBJECT
public:
    struct Source {
        QUrl url;
        QByteArray sha256;      // hex, of the zip
        qint64 size = 0;        // bytes, of the zip
        QString entry;          // the dictionary file inside the zip; empty: the whole zip into the folder given
        qint64 unpackedSize = 0;
    };
    // The Spanish -> English dictionary (release "dictionary-es-en-1", built 2026-11-22 from kaikki.org data).
    static Source spanishEnglish();
    // English definitions for English books (release "dictionary-en-1", from Open English WordNet 2025 plus).
    static Source englishDefinitions();
    // Spanish definitions (Spanish Wiktionary, for the Dictionary card).
    static Source spanishDefinitions();
    // The offline sentence translator for this system (release "offline-translator-1": Bergamot and Mozilla's
    // Spanish -> English model). Size 0 where no pack is built (then it isn't offered).
    static Source offlineTranslator();

    explicit DictionaryDownload(QObject* parent = nullptr);
    ~DictionaryDownload() override;
    bool isRunning() const { return m_running; }

    // Downloads, checks and unpacks to `outFile` (replaced if it exists); `finished` reports the result.
    void start(const Source& source, const QString& outFile);
    void cancel();

signals:
    void progress(qint64 received, qint64 total);
    void finished(const QString& outFile, const QString& error);   // error empty on success

private:
    void fail(const QString& error);
    void cleanUp();

    QNetworkAccessManager* m_net;
    QPointer<QNetworkReply> m_reply;
    QFile m_part;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    Source m_source;
    QString m_out;
    qint64 m_received = 0;
    bool m_running = false;
    bool m_canceled = false;
};

}  // namespace core

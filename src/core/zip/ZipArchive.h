// A read-only zip archive held in memory (an EPUB is a zip file). Reading entries only reads from the in-memory
// copy, so an open archive can be read from several threads at once.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

namespace core {

class ZipArchive {
public:
    // Reads the whole file. False (with a message) if it isn't a zip file this reader understands.
    bool open(const QString& path, QString* error = nullptr);
    bool openData(const QByteArray& data, QString* error = nullptr);

    bool contains(const QString& name) const;
    // The entry's uncompressed bytes. Names are case-sensitive, as in the zip; a leading "/" is ignored.
    QByteArray read(const QString& name, bool* ok = nullptr) const;
    QStringList names() const { return m_order; }
    bool hasEncryptedEntries() const { return m_encrypted; }

private:
    struct Entry {
        quint32 localHeaderOffset = 0;
        quint32 compressedSize = 0;
        quint32 size = 0;
        quint16 method = 0;
        bool encrypted = false;
    };
    bool parseCentralDirectory(QString* error);

    QByteArray m_data;
    QHash<QString, Entry> m_entries;
    QStringList m_order;
    bool m_encrypted = false;
};

}  // namespace core

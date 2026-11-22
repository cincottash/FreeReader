#include "ZipArchive.h"

#include <QFile>

#include <zlib.h>

namespace core {

namespace {

quint16 u16(const QByteArray& d, qsizetype at)
{
    const auto* p = reinterpret_cast<const uchar*>(d.constData() + at);
    return quint16(p[0] | (p[1] << 8));
}

quint32 u32(const QByteArray& d, qsizetype at)
{
    const auto* p = reinterpret_cast<const uchar*>(d.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

QString normalized(const QString& name)
{
    QString n = name;
    while (n.startsWith(QLatin1Char('/'))) n.remove(0, 1);
    return n;
}

}  // namespace

bool ZipArchive::open(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The file can't be read (%1).").arg(f.errorString());
        return false;
    }
    return openData(f.readAll(), error);
}

bool ZipArchive::openData(const QByteArray& data, QString* error)
{
    m_data = data;
    m_entries.clear();
    m_order.clear();
    m_encrypted = false;
    return parseCentralDirectory(error);
}

bool ZipArchive::parseCentralDirectory(QString* error)
{
    const qsizetype n = m_data.size();
    // The end-of-central-directory record is in the last 64 KB + 22 bytes (after an optional comment).
    qsizetype eocd = -1;
    for (qsizetype i = n - 22; i >= 0 && i >= n - 22 - 65535; --i) {
        if (u32(m_data, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        if (error) *error = QStringLiteral("The file isn't a zip archive (an EPUB file is one).");
        return false;
    }
    const quint16 count = u16(m_data, eocd + 10);
    const quint32 cdOffset = u32(m_data, eocd + 16);
    if (cdOffset == 0xFFFFFFFFu || count == 0xFFFF) {
        if (error) *error = QStringLiteral("Zip64 archives aren't supported.");
        return false;
    }
    qsizetype p = cdOffset;
    for (int i = 0; i < count; ++i) {
        if (p + 46 > n || u32(m_data, p) != 0x02014b50) {
            if (error) *error = QStringLiteral("The zip archive is damaged (its file list is incomplete).");
            return false;
        }
        Entry e;
        const quint16 flags = u16(m_data, p + 8);
        e.method = u16(m_data, p + 10);
        e.compressedSize = u32(m_data, p + 20);
        e.size = u32(m_data, p + 24);
        const quint16 nameLen = u16(m_data, p + 28), extraLen = u16(m_data, p + 30), commentLen = u16(m_data, p + 32);
        e.localHeaderOffset = u32(m_data, p + 42);
        e.encrypted = flags & 1;
        if (p + 46 + nameLen > n) break;
        const QByteArray rawName = m_data.mid(p + 46, nameLen);
        // Bit 11 marks UTF-8 names; most EPUB tools write ASCII names anyway.
        const QString name = (flags & 0x800) ? QString::fromUtf8(rawName) : QString::fromLatin1(rawName);
        if (e.encrypted) m_encrypted = true;
        if (!name.endsWith(QLatin1Char('/'))) {
            m_entries.insert(name, e);
            m_order << name;
        }
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

bool ZipArchive::contains(const QString& name) const { return m_entries.contains(normalized(name)); }

QByteArray ZipArchive::read(const QString& name, bool* ok) const
{
    if (ok) *ok = false;
    const auto it = m_entries.constFind(normalized(name));
    if (it == m_entries.cend() || it->encrypted) return {};
    const Entry& e = *it;
    const qsizetype h = e.localHeaderOffset;
    if (h + 30 > m_data.size() || u32(m_data, h) != 0x04034b50) return {};
    const qsizetype start = h + 30 + u16(m_data, h + 26) + u16(m_data, h + 28);
    if (start + qsizetype(e.compressedSize) > m_data.size()) return {};
    if (e.method == 0) {
        if (ok) *ok = true;
        return m_data.mid(start, e.compressedSize);
    }
    if (e.method != 8) return {};   // only "stored" and "deflate" appear in EPUB files

    QByteArray out;
    out.resize(qsizetype(e.size));
    z_stream zs{};
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(m_data.constData() + start));
    zs.avail_in = e.compressedSize;
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = e.size;
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return {};   // raw deflate, no zlib header
    const int rc = inflate(&zs, Z_FINISH);
    const uLong produced = zs.total_out;
    inflateEnd(&zs);
    if (rc != Z_STREAM_END && !(rc == Z_BUF_ERROR && produced == e.size)) return {};
    out.resize(qsizetype(produced));
    if (ok) *ok = true;
    return out;
}

}  // namespace core

#include "MobiFormat.h"

#include "core/format/Convert.h"
#include "core/format/text/TextFormats.h"
#include "core/html/HtmlParser.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <map>

// The layout of these files follows the public descriptions on the MobileRead wiki and the KindleUnpack
// project's notes (Palm database, PalmDOC and HUFF/CDIC compression, the MOBI and EXTH headers, INDX tables,
// KF8's FDST, skeleton and fragment tables).

namespace core {

namespace {

constexpr quint32 kNone = 0xFFFFFFFF;

quint32 be32(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 4 > d.size()) return 0;
    const auto* p = reinterpret_cast<const uchar*>(d.constData() + at);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint16 be16(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 2 > d.size()) return 0;
    const auto* p = reinterpret_cast<const uchar*>(d.constData() + at);
    return quint16((p[0] << 8) | p[1]);
}

struct Pdb {
    QByteArray data;
    std::vector<quint32> offsets;

    bool read(const QByteArray& file)
    {
        data = file;
        if (data.size() < 78) return false;
        const int n = be16(data, 76);
        if (n <= 0 || 78 + 8 * n > data.size()) return false;
        for (int i = 0; i < n; ++i) offsets.push_back(be32(data, 78 + 8 * i));
        return true;
    }
    int count() const { return int(offsets.size()); }
    QByteArray record(qint64 i) const
    {
        if (i < 0 || i >= count()) return {};
        const quint32 start = offsets[size_t(i)];
        const quint32 end = i + 1 < count() ? offsets[size_t(i + 1)] : quint32(data.size());
        if (start > end || end > quint32(data.size())) return {};
        return data.mid(start, end - start);
    }
    QByteArray type() const { return data.mid(60, 8); }   // type + creator: "BOOKMOBI", "TEXtREAd"
};

// ---------------------------------------------------------------- decompression

QByteArray palmDoc(const QByteArray& in)
{
    QByteArray out;
    out.reserve(in.size() * 2);
    const auto* d = reinterpret_cast<const uchar*>(in.constData());
    const qsizetype n = in.size();
    for (qsizetype i = 0; i < n;) {
        const uchar c = d[i++];
        if (c >= 1 && c <= 8) {
            for (int k = 0; k < c && i < n; ++k) out.append(char(d[i++]));
        } else if (c < 0x80) {
            out.append(char(c));
        } else if (c >= 0xC0) {
            out.append(' ');
            out.append(char(c ^ 0x80));
        } else if (i < n) {
            const int m = ((c << 8) | d[i++]) & 0x3FFF;
            const int dist = m >> 3, len = (m & 7) + 3;
            if (dist <= 0 || dist > out.size()) continue;
            for (int k = 0; k < len; ++k) out.append(out.at(out.size() - dist));
        }
    }
    return out;
}

class Huffcdic {
public:
    bool load(const Pdb& pdb, qint64 first, quint32 count)
    {
        const QByteArray huff = pdb.record(first);
        if (!huff.startsWith(QByteArray("HUFF\0\0\0\x18", 8))) return false;
        const quint32 off1 = be32(huff, 8), off2 = be32(huff, 12);
        for (int i = 0; i < 256; ++i) {
            const quint32 v = be32(huff, off1 + 4 * i);
            Code c;
            c.len = v & 0x1F;
            c.term = v & 0x80;
            c.max = ((quint64(v >> 8) + 1) << (32 - c.len)) - 1;
            if (c.len == 0) return false;
            dict1[i] = c;
        }
        minCode[0] = 0;
        maxCode[0] = 0;
        for (int len = 1; len <= 32; ++len) {
            minCode[len] = quint64(be32(huff, off2 + 8 * (len - 1))) << (32 - len);
            maxCode[len] = ((quint64(be32(huff, off2 + 8 * (len - 1) + 4)) + 1) << (32 - len)) - 1;
        }
        for (quint32 k = 1; k < count; ++k) {
            const QByteArray cdic = pdb.record(first + k);
            if (!cdic.startsWith(QByteArray("CDIC\0\0\0\x10", 8))) return false;
            const quint32 phrases = be32(cdic, 8), bits = be32(cdic, 12);
            const quint32 n = qMin<quint32>(1u << bits, phrases - quint32(dict.size()));
            for (quint32 j = 0; j < n; ++j) {
                const quint16 off = be16(cdic, 16 + 2 * j);
                const quint16 blen = be16(cdic, 16 + off);
                dict.push_back({cdic.mid(16 + off + 2, blen & 0x7FFF), bool(blen & 0x8000)});
            }
        }
        return !dict.empty();
    }

    QByteArray unpack(const QByteArray& input, int depth = 0)
    {
        QByteArray out;
        if (depth > 32) return out;
        QByteArray data = input;
        data.append(QByteArray(8, '\0'));
        qint64 bitsLeft = qint64(input.size()) * 8;
        qsizetype pos = 0;
        auto q = [&](qsizetype p) {
            quint64 v = 0;
            for (int k = 0; k < 8; ++k) v = (v << 8) | uchar(data.at(p + k));
            return v;
        };
        quint64 x = q(pos);
        int n = 32;
        while (true) {
            if (n <= 0) {
                pos += 4;
                if (pos + 8 > data.size()) break;
                x = q(pos);
                n += 32;
            }
            const quint64 code = (x >> n) & 0xFFFFFFFFull;
            const Code& c = dict1[code >> 24];
            int len = c.len;
            quint64 max = c.max;
            if (!c.term) {
                while (len < 32 && code < minCode[len]) ++len;
                max = maxCode[len];
            }
            n -= len;
            bitsLeft -= len;
            if (bitsLeft < 0) break;
            const quint64 r = (max - code) >> (32 - len);
            if (r >= dict.size()) break;
            Entry& e = dict[size_t(r)];
            if (!e.done) {
                e.done = true;   // guards against a phrase that contains itself
                e.data = unpack(e.data, depth + 1);
            }
            out += e.data;
        }
        return out;
    }

private:
    struct Code {
        int len = 0;
        bool term = false;
        quint64 max = 0;
    };
    struct Entry {
        QByteArray data;
        bool done = false;
    };
    Code dict1[256];
    quint64 minCode[33] = {}, maxCode[33] = {};
    std::vector<Entry> dict;
};

// The bytes at the end of a text record that aren't text (the header's "extra data flags").
int trailingSize(const QByteArray& rec, quint16 flags)
{
    auto entrySize = [&](qsizetype size) -> int {
        int result = 0, bitpos = 0;
        while (size > 0) {
            const uchar v = uchar(rec.at(size - 1));
            result |= (v & 0x7F) << bitpos;
            bitpos += 7;
            --size;
            if ((v & 0x80) || bitpos >= 28) break;
        }
        return result;
    };
    int num = 0;
    for (quint16 f = flags >> 1; f; f >>= 1)
        if (f & 1) num += entrySize(rec.size() - num);
    if ((flags & 1) && rec.size() - num - 1 >= 0) num += (uchar(rec.at(rec.size() - num - 1)) & 0x3) + 1;
    return qBound(0, num, int(rec.size()));
}

// ---------------------------------------------------------------- headers

struct Header {
    qint64 start = 0;   // the record of this header (0, or the KF8 part of a combined file)
    quint16 compression = 1, textRecords = 0, encryption = 0;
    quint32 textLength = 0;
    bool mobi = false;
    quint32 mobiLength = 0, codepage = 1252, version = 0;
    quint32 firstImage = kNone, huffOffset = kNone, huffCount = 0, exthFlags = 0;
    quint32 fdst = kNone, ncx = kNone, frag = kNone, skel = kNone;
    quint16 extraFlags = 0;
    QString fullName;
    QMultiHash<int, QByteArray> exth;

    bool parse(const QByteArray& r0, qint64 at)
    {
        start = at;
        if (r0.size() < 16) return false;
        compression = be16(r0, 0);
        textLength = be32(r0, 4);
        textRecords = be16(r0, 8);
        encryption = be16(r0, 12);
        mobi = r0.mid(16, 4) == "MOBI";
        if (!mobi) return true;
        mobiLength = be32(r0, 20);
        codepage = be32(r0, 28);
        version = be32(r0, 36);
        const quint32 nameOff = be32(r0, 0x54), nameLen = be32(r0, 0x58);
        firstImage = be32(r0, 0x6C);
        huffOffset = be32(r0, 0x70);
        huffCount = be32(r0, 0x74);
        exthFlags = be32(r0, 0x80);
        if (mobiLength >= 0xE4) extraFlags = be16(r0, 0xF2);
        if (version >= 8) {
            fdst = be32(r0, 0xC0);
            frag = be32(r0, 0xF8);
            skel = be32(r0, 0xFC);
        }
        if (mobiLength + 16 > 0xF4) ncx = be32(r0, 0xF4);
        const QByteArray name = r0.mid(nameOff, nameLen);
        fullName = codepage == 65001 ? QString::fromUtf8(name) : convert::decodeAs(name, QStringLiteral("windows-1252"));
        if (exthFlags & 0x40) {
            const qsizetype e = 16 + mobiLength;
            if (r0.mid(e, 4) == "EXTH") {
                const quint32 count = be32(r0, e + 8);
                qsizetype p = e + 12;
                for (quint32 i = 0; i < count && p + 8 <= r0.size(); ++i) {
                    const quint32 type = be32(r0, p), len = be32(r0, p + 4);
                    if (len < 8 || p + len > r0.size()) break;
                    exth.insert(int(type), r0.mid(p + 8, len - 8));
                    p += len;
                }
            }
        }
        return true;
    }
    // A record index from this header (relative to its own part), or -1.
    qint64 rec(quint32 v) const { return v == kNone ? -1 : start + qint64(v); }
    QString text(const QByteArray& b) const { return codepage == 65001 ? QString::fromUtf8(b) : convert::decodeAs(b, QStringLiteral("windows-1252")); }
    QString exthText(int type) const { return exth.contains(type) ? text(exth.value(type)).trimmed() : QString(); }
};

QByteArray bookText(const Pdb& pdb, const Header& h, QString* error)
{
    Huffcdic huff;
    if (h.compression == 17480 && !huff.load(pdb, h.rec(h.huffOffset), h.huffCount)) {
        if (error) *error = QStringLiteral("The book's text can't be unpacked (its HUFF/CDIC tables are damaged).");
        return {};
    }
    QByteArray text;
    for (int i = 1; i <= h.textRecords; ++i) {
        QByteArray r = pdb.record(h.start + i);
        r.chop(trailingSize(r, h.extraFlags));
        if (h.compression == 2) text += palmDoc(r);
        else if (h.compression == 17480) text += huff.unpack(r);
        else text += r;
    }
    if (h.textLength && quint32(text.size()) > h.textLength) text.truncate(h.textLength);
    return text;
}

// ---------------------------------------------------------------- INDX tables

struct IndexEntry {
    QByteArray text;
    QHash<int, std::vector<quint32>> tags;
};

std::pair<int, quint32> forwardVwi(const QByteArray& d, qsizetype off)
{
    quint32 value = 0;
    int used = 0;
    while (off + used < d.size()) {
        const uchar v = uchar(d.at(off + used));
        ++used;
        value = (value << 7) | (v & 0x7F);
        if (v & 0x80) break;
    }
    return {used, value};
}

std::vector<IndexEntry> readIndex(const Pdb& pdb, qint64 idx, QHash<quint32, QByteArray>* cncx)
{
    std::vector<IndexEntry> out;
    const QByteArray d = pdb.record(idx);
    if (idx < 0 || !d.startsWith("INDX")) return out;
    const quint32 headerLen = be32(d, 4), count = be32(d, 24), nctoc = be32(d, 52);
    // The tag table.
    struct Tag {
        int tag, perEntry, mask, end;
    };
    std::vector<Tag> table;
    int controlBytes = 0;
    if (d.mid(headerLen, 4) == "TAGX") {
        const quint32 firstEntry = be32(d, headerLen + 4);
        controlBytes = int(be32(d, headerLen + 8));
        for (quint32 i = 12; i + 4 <= firstEntry; i += 4) {
            const qsizetype p = headerLen + i;
            table.push_back({uchar(d.at(p)), uchar(d.at(p + 1)), uchar(d.at(p + 2)), uchar(d.at(p + 3))});
        }
    }
    if (cncx) {
        for (quint32 j = 0; j < nctoc; ++j) {
            const QByteArray c = pdb.record(idx + count + 1 + j);
            qsizetype off = 0;
            while (off < c.size() && c.at(off) != '\0') {
                const auto [used, len] = forwardVwi(c, off);
                cncx->insert(j * 0x10000 + quint32(off), c.mid(off + used, len));
                off += used + qsizetype(len);
            }
        }
    }
    for (quint32 i = 1; i <= count; ++i) {
        const QByteArray r = pdb.record(idx + i);
        if (!r.startsWith("INDX")) continue;
        const quint32 idxt = be32(r, 20), n = be32(r, 24);
        std::vector<quint32> pos;
        for (quint32 j = 0; j < n; ++j) pos.push_back(be16(r, idxt + 4 + 2 * j));
        pos.push_back(idxt);
        for (quint32 j = 0; j < n; ++j) {
            const qsizetype sp = pos[j], ep = pos[j + 1];
            if (sp >= r.size()) break;
            IndexEntry e;
            const int textLen = uchar(r.at(sp));
            e.text = r.mid(sp + 1, textLen);
            // The tag values, as the control bytes say.
            const qsizetype cb = sp + 1 + textLen;
            qsizetype dataStart = cb + controlBytes;
            int cbIndex = 0;
            struct Pending {
                int tag, count;
                qint64 bytes;
                int perEntry;
            };
            std::vector<Pending> pending;
            for (const Tag& t : table) {
                if (t.end == 1) {
                    ++cbIndex;
                    continue;
                }
                const int cbyte = uchar(r.at(cb + cbIndex));
                int value = cbyte & t.mask;
                if (!value) continue;
                if (value == t.mask) {
                    int bits = 0;
                    for (int m = t.mask; m; m >>= 1) bits += m & 1;
                    if (bits > 1) {
                        const auto [used, v] = forwardVwi(r, dataStart);
                        dataStart += used;
                        pending.push_back({t.tag, -1, qint64(v), t.perEntry});
                    } else {
                        pending.push_back({t.tag, 1, -1, t.perEntry});
                    }
                } else {
                    int mask = t.mask;
                    while (!(mask & 1)) {
                        mask >>= 1;
                        value >>= 1;
                    }
                    pending.push_back({t.tag, value, -1, t.perEntry});
                }
            }
            for (const Pending& p : pending) {
                std::vector<quint32> values;
                if (p.count >= 0) {
                    for (int k = 0; k < p.count * p.perEntry && dataStart < ep; ++k) {
                        const auto [used, v] = forwardVwi(r, dataStart);
                        dataStart += used;
                        values.push_back(v);
                    }
                } else {
                    qint64 consumed = 0;
                    while (consumed < p.bytes && dataStart < ep) {
                        const auto [used, v] = forwardVwi(r, dataStart);
                        dataStart += used;
                        consumed += used;
                        values.push_back(v);
                    }
                }
                e.tags.insert(p.tag, values);
            }
            out.push_back(e);
        }
    }
    return out;
}

quint32 base32(const QByteArray& s)
{
    quint32 v = 0;
    for (const char c : s) {
        const int d = c >= '0' && c <= '9' ? c - '0' : (c >= 'A' && c <= 'V') ? c - 'A' + 10 : (c >= 'a' && c <= 'v') ? c - 'a' + 10 : 0;
        v = v * 32 + quint32(d);
    }
    return v;
}

// ---------------------------------------------------------------- pictures

// The book's pictures: "images/imgNNNNN.ext" for the Nth resource record (1 = the first image record).
QHash<int, QString> addImages(MemoryBook& book, const Pdb& pdb, const Header& h)
{
    QHash<int, QString> paths;
    const qint64 first = h.rec(h.firstImage);
    if (first < 0) return paths;
    for (qint64 i = first; i < pdb.count(); ++i) {
        const QByteArray r = pdb.record(i);
        if (r.startsWith("BOUNDARY") || r.startsWith("EOF")) break;   // the end of this part's resources
        const QString type = convert::imageType(r);
        if (type.isEmpty() || type == QLatin1String("image/svg+xml")) continue;
        const int n = int(i - first + 1);
        const QString path = QStringLiteral("images/img%1.%2").arg(n, 5, 10, QLatin1Char('0')).arg(convert::imageSuffix(type));
        book.addFile(path, r);
        paths.insert(n, path);
    }
    return paths;
}

void readMetadata(MemoryBook& book, const Header& h, const QHash<int, QString>& images)
{
    BookMetadata& m = book.metadata;
    m.title = h.exthText(503);
    if (m.title.isEmpty()) m.title = h.fullName.trimmed();
    for (const QByteArray& a : h.exth.values(100)) {
        const QString author = h.text(a).trimmed();
        if (!author.isEmpty() && !m.authors.contains(author)) m.authors.prepend(author);
    }
    // "Coloma, Luis" -> "Luis Coloma".
    for (QString& a : m.authors) {
        static const QRegularExpression lastFirst(QStringLiteral("^([^,]+),\\s*([^,]+)$"));
        if (const auto x = lastFirst.match(a); x.hasMatch() && !x.captured(2).contains(QLatin1Char(' '))) a = x.captured(2) + QLatin1Char(' ') + x.captured(1);
    }
    m.publisher = h.exthText(101);
    m.description = h.exthText(103);
    m.identifier = h.exthText(104);
    if (m.identifier.isEmpty()) m.identifier = h.exthText(113);
    m.language = h.exthText(524);
    if (h.exth.contains(201)) {
        const QByteArray v = h.exth.value(201);
        const int n = int(be32(v, 0)) + 1;
        if (images.contains(n)) m.coverPath = images.value(n);
    }
}

// Headings of a part, for books without a table of contents.
QString partTitle(const QString& html)
{
    static const QRegularExpression re(QStringLiteral("<h([1-3])\\b[^>]*>(.*?)</h\\1\\s*>"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(html);
    if (!m.hasMatch()) return {};
    const auto doc = dom::parseText(QStringLiteral("<div>") + m.captured(2) + QStringLiteral("</div>"));
    return doc->root->textContent().simplified().left(120);
}

// ---------------------------------------------------------------- MOBI (version 6 and older)

bool openMobi6(MemoryBook& book, const Pdb& pdb, const Header& h, QString* error)
{
    QByteArray raw = bookText(pdb, h, error);
    if (raw.isEmpty()) {
        if (error && error->isEmpty()) *error = QStringLiteral("The book has no text.");
        return false;
    }
    const QHash<int, QString> images = addImages(book, pdb, h);
    readMetadata(book, h, images);

    // The contents (NCX index): each entry's place in the text and its label.
    struct NcxEntry {
        quint32 pos;
        QString label;
        int depth;
    };
    std::vector<NcxEntry> ncx;
    QHash<quint32, QByteArray> cncx;
    for (const IndexEntry& e : readIndex(pdb, h.rec(h.ncx), &cncx)) {
        if (!e.tags.contains(1) || e.tags.value(1).empty()) continue;
        NcxEntry n;
        n.pos = e.tags.value(1)[0];
        n.label = e.tags.contains(3) && !e.tags.value(3).empty() ? h.text(cncx.value(e.tags.value(3)[0])) : QString();
        n.depth = e.tags.contains(4) && !e.tags.value(4).empty() ? int(e.tags.value(4)[0]) : 0;
        if (!n.label.trimmed().isEmpty()) ncx.push_back(n);
    }

    // Anchors where links ("filepos") and contents entries point: inserted from the end so places stay right.
    static const QRegularExpression fileposRe(QStringLiteral("filepos\\s*=\\s*[\"']?0*(\\d+)[\"']?"), QRegularExpression::CaseInsensitiveOption);
    QSet<quint32> targets;
    {
        const QString latin = QString::fromLatin1(raw);
        auto it = fileposRe.globalMatch(latin);
        while (it.hasNext()) targets.insert(it.next().captured(1).toUInt());
    }
    for (const NcxEntry& n : ncx) targets.insert(n.pos);
    std::vector<quint32> sorted(targets.begin(), targets.end());
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    for (const quint32 p : sorted) {
        if (p >= quint32(raw.size())) continue;
        qsizetype at = qsizetype(p);
        const qsizetype lt = raw.lastIndexOf('<', at), gt = raw.lastIndexOf('>', at - 1);
        if (lt > gt && lt >= 0) at = lt;   // inside a tag: before it
        raw.insert(at, QByteArray("<a id=\"fp") + QByteArray::number(p) + "\"></a>");
    }
    QString text = h.text(raw);
    text.replace(fileposRe, QStringLiteral("href=\"#fp\\1\""));
    // Pictures: recindex is the resource's number.
    static const QRegularExpression recRe(QStringLiteral("\\b(?:hi|lo)?recindex\\s*=\\s*[\"']?0*(\\d+)[\"']?"), QRegularExpression::CaseInsensitiveOption);
    {
        QString out;
        qsizetype last = 0;
        auto it = recRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            out += text.mid(last, m.capturedStart() - last);
            out += QStringLiteral("src=\"%1\"").arg(images.value(m.captured(1).toInt()));
            last = m.capturedEnd();
        }
        text = out + text.mid(last);
    }
    // The body only; parts at each page break.
    static const QRegularExpression bodyOpen(QStringLiteral("<body\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    if (const auto b = bodyOpen.match(text); b.hasMatch()) text = text.mid(b.capturedEnd());
    if (const qsizetype e = text.lastIndexOf(QLatin1String("</body"), -1, Qt::CaseInsensitive); e >= 0) text.truncate(e);
    static const QRegularExpression pageBreak(QStringLiteral("<mbp:pagebreak\\s*/?>"), QRegularExpression::CaseInsensitiveOption);
    QStringList parts = text.split(pageBreak);
    // Very small parts (a break right after another) join the next one.
    QStringList merged;
    for (const QString& p : parts) {
        if (!merged.isEmpty() && merged.last().size() < 200 && !merged.last().contains(QLatin1String("<img"), Qt::CaseInsensitive)) merged.last() += p;
        else merged << p;
    }
    parts = merged;
    QHash<QString, int> idPart;
    static const QRegularExpression idRe(QStringLiteral("\\bid=\"(fp\\d+)\""));
    for (int i = 0; i < parts.size(); ++i) {
        auto it = idRe.globalMatch(parts[i]);
        while (it.hasNext()) idPart.insert(it.next().captured(1), i);
    }
    auto file = [](int i) { return QStringLiteral("part%1.html").arg(i + 1, 4, 10, QLatin1Char('0')); };
    static const QRegularExpression hrefRe(QStringLiteral("href=\"#(fp\\d+)\""));
    const QString lang = book.metadata.language;
    for (int i = 0; i < parts.size(); ++i) {
        QString p = parts[i];
        QString out;
        qsizetype last = 0;
        auto it = hrefRe.globalMatch(p);
        while (it.hasNext()) {
            const auto m = it.next();
            out += p.mid(last, m.capturedStart() - last);
            const int target = idPart.value(m.captured(1), i);
            out += target == i ? m.captured() : QStringLiteral("href=\"%1#%2\"").arg(file(target), m.captured(1));
            last = m.capturedEnd();
        }
        p = out + p.mid(last);
        const QString title = ncx.empty() ? partTitle(p) : QString();
        book.addChapter(file(i), convert::page(title.isEmpty() ? book.metadata.title : title, p, lang), title);
    }
    for (const NcxEntry& n : ncx) {
        const QString id = QStringLiteral("fp%1").arg(n.pos);
        if (idPart.contains(id)) book.addTocEntry(n.label, file(idPart.value(id)) + QLatin1Char('#') + id, n.depth);
    }
    return !book.spine.empty();
}

// ---------------------------------------------------------------- KF8 (AZW3)

bool openKf8(MemoryBook& book, const Pdb& pdb, const Header& h, QString* error)
{
    const QByteArray raw = bookText(pdb, h, error);
    if (raw.isEmpty()) return false;
    const QHash<int, QString> images = addImages(book, pdb, h);
    readMetadata(book, h, images);

    // Flows: the text (flow 0), then style sheets and SVG pictures.
    std::vector<quint32> bounds;
    const QByteArray fdst = pdb.record(h.rec(h.fdst));
    if (fdst.startsWith("FDST")) {
        const quint32 n = be32(fdst, 8);
        for (quint32 i = 0; i < n; ++i) bounds.push_back(be32(fdst, 12 + 8 * i));
    }
    if (bounds.empty()) bounds.push_back(0);
    bounds.push_back(quint32(raw.size()));
    std::vector<QByteArray> flows;
    for (size_t i = 0; i + 1 < bounds.size(); ++i) flows.push_back(raw.mid(bounds[i], qMax<qint64>(0, qint64(bounds[i + 1]) - bounds[i])));
    const QByteArray text = flows.empty() ? raw : flows[0];

    struct Skel {
        int fragments;
        quint32 pos, len;
    };
    struct Frag {
        quint32 insert;
        quint32 file, start, len;
    };
    std::vector<Skel> skels;
    std::vector<Frag> frags;
    for (const IndexEntry& e : readIndex(pdb, h.rec(h.skel), nullptr)) {
        const auto f = e.tags.value(1), p = e.tags.value(6);
        if (f.empty() || p.size() < 2) continue;
        skels.push_back({int(f[0]), p[0], p[1]});
    }
    for (const IndexEntry& e : readIndex(pdb, h.rec(h.frag), nullptr)) {
        const auto file = e.tags.value(3), p = e.tags.value(6);
        if (p.size() < 2) continue;
        frags.push_back({e.text.toUInt(), file.empty() ? 0u : file[0], p[0], p[1]});
    }
    if (skels.empty()) {
        if (error) *error = QStringLiteral("The book is damaged: its KF8 part has no chapters.");
        return false;
    }
    struct Part {
        QByteArray html;
        quint32 start, end;   // its place in the assembled text
    };
    std::vector<Part> parts;
    size_t fragPtr = 0;
    for (const Skel& s : skels) {
        quint32 base = s.pos + s.len;
        QByteArray skeleton = text.mid(s.pos, s.len);
        for (int i = 0; i < s.fragments && fragPtr < frags.size(); ++i, ++fragPtr) {
            const Frag& f = frags[fragPtr];
            const QByteArray slice = text.mid(base, f.len);
            qsizetype at = qsizetype(f.insert) - qsizetype(s.pos);
            at = qBound<qsizetype>(0, at, skeleton.size());
            skeleton.insert(at, slice);
            base += f.len;
        }
        parts.push_back({skeleton, s.pos, base});
    }
    auto partOf = [&](quint32 pos) -> int {
        for (size_t i = 0; i < parts.size(); ++i)
            if (pos >= parts[i].start && pos < parts[i].end) return int(i);
        return -1;
    };
    auto file = [](int i) { return QStringLiteral("part%1.html").arg(i + 1, 4, 10, QLatin1Char('0')); };
    // The id (or Kindle's aid) of the tag at or before a place in a part.
    auto idBefore = [&](int pn, qsizetype offset) -> QByteArray {
        const QByteArray& b = parts[size_t(pn)].html;
        offset = qBound<qsizetype>(0, offset, b.size());
        const qsizetype gt = b.indexOf('>', offset), lt = b.indexOf('<', offset);
        if (lt == offset || (gt >= 0 && (lt < 0 || gt < lt))) offset = gt + 1;
        static const QRegularExpression idRe(QStringLiteral("\\s(?:id|name)\\s*=\\s*['\"]([^'\"]*)['\"]"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression aidRe(QStringLiteral("\\said\\s*=\\s*['\"]([^'\"]+)['\"]"), QRegularExpression::CaseInsensitiveOption);
        qsizetype end = offset;
        while (end > 0) {
            const qsizetype open = b.lastIndexOf('<', end - 1);
            if (open < 0) break;
            const qsizetype close = b.indexOf('>', open);
            const QString tag = QString::fromUtf8(b.mid(open, (close < 0 ? b.size() : close + 1) - open));
            if (tag.startsWith(QLatin1String("<body"), Qt::CaseInsensitive)) return {};
            if (!tag.startsWith(QLatin1String("<meta"), Qt::CaseInsensitive)) {
                if (const auto m = idRe.match(tag); m.hasMatch()) return m.captured(1).toUtf8();
                if (const auto m = aidRe.match(tag); m.hasMatch()) return "aid-" + m.captured(1).toUtf8();
            }
            end = open;
        }
        return {};
    };
    auto target = [&](quint32 fid, quint32 off) -> QString {
        if (fid >= frags.size()) return {};
        const quint32 pos = frags[fid].insert + off;
        int pn = partOf(pos);
        if (pn < 0) pn = int(qMin<size_t>(frags[fid].file, parts.size() - 1));
        const QByteArray id = idBefore(pn, qsizetype(pos) - qsizetype(parts[size_t(pn)].start));
        return id.isEmpty() ? file(pn) : file(pn) + QLatin1Char('#') + QString::fromUtf8(id);
    };

    // Contents (NCX): each entry's place (fragment and offset) and label.
    QHash<quint32, QByteArray> cncx;
    struct TocItem {
        QString label, href;
        int depth;
    };
    std::vector<TocItem> toc;
    for (const IndexEntry& e : readIndex(pdb, h.rec(h.ncx), &cncx)) {
        const auto p = e.tags.value(6), label = e.tags.value(3), depth = e.tags.value(4);
        if (p.size() < 2 || label.empty()) continue;
        toc.push_back({QString::fromUtf8(cncx.value(label[0])), target(p[0], p[1]), depth.empty() ? 0 : int(depth[0])});
    }

    // Style sheets and SVG flows.
    static const QRegularExpression embedRe(QStringLiteral("kindle:embed:([0-9A-Va-v]{4})(?:\\?mime=[^\"')\\s]*)?"));
    auto fixEmbeds = [&](QString s, const QString& prefix) {
        QString out;
        qsizetype last = 0;
        auto it = embedRe.globalMatch(s);
        while (it.hasNext()) {
            const auto m = it.next();
            out += s.mid(last, m.capturedStart() - last);
            const QString img = images.value(int(base32(m.captured(1).toLatin1())));
            out += img.isEmpty() ? QStringLiteral("missing") : prefix + img;
            last = m.capturedEnd();
        }
        return out + s.mid(last);
    };
    for (size_t i = 1; i < flows.size(); ++i) {
        const QString f = QString::fromUtf8(flows[i]);
        const bool svg = f.trimmed().startsWith(QLatin1String("<svg")) || f.contains(QLatin1String("<svg"));
        book.addFile(svg ? QStringLiteral("images/flow%1.svg").arg(i, 4, 10, QLatin1Char('0')) : QStringLiteral("styles/flow%1.css").arg(i, 4, 10, QLatin1Char('0')),
                     fixEmbeds(f, svg ? QStringLiteral("../") : QStringLiteral("../")).toUtf8());
    }
    static const QRegularExpression flowRe(QStringLiteral("kindle:flow:([0-9A-Va-v]{4})\\?mime=([^\"'\\s]+)"));
    static const QRegularExpression posRe(QStringLiteral("kindle:pos:fid:([0-9A-Va-v]{4}):off:([0-9A-Va-v]{10})"));
    static const QRegularExpression aidAttr(QStringLiteral("<([A-Za-z][^>]*?)\\said\\s*=\\s*['\"]([^'\"]+)['\"]([^>]*)>"));
    for (size_t i = 0; i < parts.size(); ++i) {
        QString p = QString::fromUtf8(parts[i].html);
        // Links to places in the book.
        {
            QString out;
            qsizetype last = 0;
            auto it = posRe.globalMatch(p);
            while (it.hasNext()) {
                const auto m = it.next();
                out += p.mid(last, m.capturedStart() - last);
                out += target(base32(m.captured(1).toLatin1()), base32(m.captured(2).toLatin1()));
                last = m.capturedEnd();
            }
            p = out + p.mid(last);
        }
        {
            QString out;
            qsizetype last = 0;
            auto it = flowRe.globalMatch(p);
            while (it.hasNext()) {
                const auto m = it.next();
                out += p.mid(last, m.capturedStart() - last);
                const int n = int(base32(m.captured(1).toLatin1()));
                out += m.captured(2).contains(QLatin1String("svg")) ? QStringLiteral("images/flow%1.svg").arg(n, 4, 10, QLatin1Char('0'))
                                                                     : QStringLiteral("styles/flow%1.css").arg(n, 4, 10, QLatin1Char('0'));
                last = m.capturedEnd();
            }
            p = out + p.mid(last);
        }
        p = fixEmbeds(p, QString());
        // Kindle's aid attributes become ids where a tag has none, so links that point at them arrive.
        {
            QString out;
            qsizetype last = 0;
            auto it = aidAttr.globalMatch(p);
            while (it.hasNext()) {
                const auto m = it.next();
                out += p.mid(last, m.capturedStart() - last);
                const QString rest = m.captured(1) + m.captured(3);
                const bool hasId = QRegularExpression(QStringLiteral("\\sid\\s*=")).match(rest).hasMatch();
                out += hasId ? QStringLiteral("<%1%2>").arg(m.captured(1), m.captured(3))
                             : QStringLiteral("<%1 id=\"aid-%2\"%3>").arg(m.captured(1), m.captured(2), m.captured(3));
                last = m.capturedEnd();
            }
            p = out + p.mid(last);
        }
        const QString title = toc.empty() ? partTitle(p) : QString();
        book.addChapter(file(int(i)), p.toUtf8(), title);
    }
    for (const TocItem& t : toc)
        if (!t.href.isEmpty()) book.addTocEntry(t.label, t.href, t.depth);
    return !book.spine.empty();
}

QString drmMessage()
{
    return QStringLiteral("This Kindle book is protected by DRM, so Biblioglot can't open it. Books bought from Amazon can be "
                          "read in the Kindle app; DRM-free MOBI and AZW3 files (and EPUB) open here.");
}

}  // namespace

bool MobiFormat::canOpen(const QString& path, const QByteArray& head) const
{
    const QByteArray type = head.mid(60, 8);
    if (type == "BOOKMOBI" || type == "TEXtREAd") return true;
    if (head.startsWith("\xEA" "DRMION\xEE") || head.startsWith("CONT\x02\x00")) return true;   // KFX: a message
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("mobi") || s == QLatin1String("azw") || s == QLatin1String("azw3") || s == QLatin1String("prc") || s == QLatin1String("kfx")
           || (s == QLatin1String("pdb") && head.size() >= 68);
}

std::shared_ptr<Book> MobiFormat::open(const QString& path, QString* error) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The file can't be read: %1").arg(f.errorString());
        return nullptr;
    }
    const QByteArray data = f.readAll();
    if (data.startsWith("\xEA" "DRMION\xEE")) {
        if (error) *error = drmMessage();
        return nullptr;
    }
    if (data.startsWith("CONT\x02\x00") || QFileInfo(path).suffix().compare(QLatin1String("kfx"), Qt::CaseInsensitive) == 0) {
        if (error) *error = QStringLiteral("This is a KFX book (Amazon's newest format), which Biblioglot can't open. A MOBI, AZW3 or EPUB copy opens.");
        return nullptr;
    }
    Pdb pdb;
    if (!pdb.read(data)) {
        if (error) *error = QStringLiteral("The book is damaged: it isn't a Kindle or Palm book.");
        return nullptr;
    }
    const QByteArray type = pdb.type();
    if (type == "PNRdPPrs") {
        if (error) *error = QStringLiteral("This is an eReader (Palm) book, which Biblioglot can't open.");
        return nullptr;
    }
    if (type != "BOOKMOBI" && type != "TEXtREAd") {
        if (error) *error = QStringLiteral("This Palm database isn't a book (its type is %1).").arg(QString::fromLatin1(type).trimmed());
        return nullptr;
    }
    Header h;
    if (!h.parse(pdb.record(0), 0)) {
        if (error) *error = QStringLiteral("The book is damaged: its header can't be read.");
        return nullptr;
    }
    if (h.encryption != 0) {
        if (error) *error = drmMessage();
        return nullptr;
    }
    if (h.compression != 1 && h.compression != 2 && h.compression != 17480) {
        if (error) *error = QStringLiteral("The book's text is packed in a way Biblioglot doesn't know (%1).").arg(h.compression);
        return nullptr;
    }
    auto book = std::make_shared<MemoryBook>();
    book->filePath = path;
    QString err;
    bool ok = false;
    if (!h.mobi) {
        // PalmDOC: plain text.
        const QByteArray raw = bookText(pdb, h, &err);
        book->metadata.title = QString::fromLatin1(data.left(32).split('\0').first()).replace(QLatin1Char('_'), QLatin1Char(' ')).trimmed();
        text::fromPlainText(*book, convert::decodeText(raw));
        ok = !book->spine.empty();
    } else {
        // A combined file: its KF8 part comes after a BOUNDARY record (EXTH 121 says where).
        Header k8;
        bool haveK8 = h.version >= 8;
        if (haveK8) k8 = h;
        if (!haveK8 && h.exth.contains(121)) {
            const qint64 at = be32(h.exth.value(121), 0);
            for (const qint64 r : {at, at + 1}) {
                const QByteArray r0 = pdb.record(r);
                if (r0.mid(16, 4) == "MOBI" && k8.parse(r0, r) && k8.version >= 8 && k8.encryption == 0) {
                    haveK8 = true;
                    break;
                }
            }
        }
        if (haveK8) {
            ok = openKf8(*book, pdb, k8, &err);
            if (!ok) book = std::make_shared<MemoryBook>(), book->filePath = path;
        }
        if (!ok && h.version < 8) ok = openMobi6(*book, pdb, h, &err);
    }
    if (!ok) {
        if (error) *error = err.isEmpty() ? QStringLiteral("The book has no chapters to show.") : err;
        return nullptr;
    }
    if (book->metadata.title.isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();
    if (book->metadata.language.isEmpty()) {
        QString sample;
        for (size_t i = 0; i < book->spine.size() && sample.size() < 40000; ++i) {
            const auto doc = dom::parse(book->resource(book->spine[i].path));
            sample += doc->root->textContent();
        }
        book->metadata.language = convert::guessLanguage(sample);
    }
    return book;
}

}  // namespace core

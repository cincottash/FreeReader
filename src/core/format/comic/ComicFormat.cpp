#include "ComicFormat.h"

#include "core/format/Convert.h"
#include "core/html/HtmlParser.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

namespace core {

namespace comic {

Archive archiveKind(const QByteArray& head)
{
    if (head.startsWith("PK\x03\x04") || head.startsWith("PK\x05\x06")) return Archive::Zip;
    if (head.startsWith("Rar!\x1A\x07")) return Archive::Rar;
    if (head.startsWith("7z\xBC\xAF\x27\x1C")) return Archive::SevenZip;
    if (head.size() >= 262 && head.mid(257, 5) == "ustar") return Archive::Tar;
    // An old (v7) tar: a file name, then octal numbers ending in a space or NUL at fixed places.
    if (head.size() >= 512) {
        const QByteArray size = head.mid(124, 12);
        bool octal = !size.trimmed().isEmpty();
        for (const char c : size)
            if (!(c == ' ' || c == '\0' || (c >= '0' && c <= '7'))) octal = false;
        if (octal && head[0] != '\0') return Archive::Tar;
    }
    return Archive::Unknown;
}

bool canReadArchive(Archive kind)
{
#ifdef HAVE_LIBARCHIVE
    return kind != Archive::Unknown;
#else
    return kind == Archive::Zip || kind == Archive::Tar;
#endif
}

QStringList pageNames(const QStringList& names)
{
    QStringList pages;
    for (const QString& n : names) {
        if (n.endsWith(QLatin1Char('/'))) continue;
        const QString lower = n.toLower();
        if (lower.startsWith(QLatin1String("__macosx/")) || lower.contains(QLatin1String("/__macosx/"))) continue;
        const QString file = QFileInfo(n).fileName();
        if (file.startsWith(QLatin1Char('.')) || file.compare(QLatin1String("thumbs.db"), Qt::CaseInsensitive) == 0) continue;
        const QString s = QFileInfo(n).suffix().toLower();
        static const QStringList kinds = {"jpg", "jpeg", "png", "gif", "webp", "bmp", "tif", "tiff", "avif", "jxl"};
        if (kinds.contains(s)) pages << n;
    }
    std::stable_sort(pages.begin(), pages.end(), convert::naturalLess);
    return pages;
}

}  // namespace comic

namespace {

// A tar file read in place: each entry's place and size, found once.
struct TarIndex {
    struct Entry {
        qint64 offset = 0, size = 0;
    };
    QHash<QString, Entry> entries;
    QStringList order;

    bool build(const QString& path, QString* error)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("The file can't be read: %1").arg(f.errorString());
            return false;
        }
        qint64 pos = 0;
        QString longName;
        while (pos + 512 <= f.size()) {
            f.seek(pos);
            const QByteArray h = f.read(512);
            if (h.size() < 512 || h.count('\0') == 512) break;
            auto field = [&](int at, int len) { return QByteArray(h.constData() + at, len).split('\0').first(); };
            bool ok = false;
            const qint64 size = field(124, 12).trimmed().toLongLong(&ok, 8);
            if (!ok && !field(124, 12).trimmed().isEmpty()) break;
            const char type = h[156];
            QString name = QString::fromUtf8(field(0, 100));
            const QByteArray prefix = h.mid(257, 5) == "ustar" ? field(345, 155) : QByteArray();
            if (!prefix.isEmpty()) name = QString::fromUtf8(prefix) + QLatin1Char('/') + name;
            const qint64 data = pos + 512;
            if (type == 'L') {   // GNU long name: the next entry's name
                f.seek(data);
                longName = QString::fromUtf8(f.read(size).split('\0').first());
            } else if (type == 'x') {   // pax header: "len path=value\n"
                f.seek(data);
                for (const QByteArray& line : f.read(size).split('\n'))
                    if (const int at = line.indexOf(" path="); at > 0) longName = QString::fromUtf8(line.mid(at + 6));
            } else {
                if (!longName.isEmpty()) {
                    name = longName;
                    longName.clear();
                }
                if (type == '0' || type == '\0' || type == '7') {
                    if (!entries.contains(name)) order << name;
                    entries.insert(name, {data, size});
                }
            }
            pos = data + ((size + 511) / 512) * 512;
        }
        if (order.isEmpty()) {
            if (error) *error = QStringLiteral("The archive is empty or damaged.");
            return false;
        }
        return true;
    }
};

class ComicBook : public MemoryBook {
public:
    comic::Archive kind = comic::Archive::Unknown;
    TarIndex tar;
    QStringList pages;   // archive entry of each page, in order
    QHash<QString, int> alias;   // "img/0001.jpg" -> its page

    QByteArray entry(const QString& name, bool* ok) const
    {
        if (kind == comic::Archive::Zip) return zip.read(name, ok);
        if (kind == comic::Archive::Tar) {
            const auto it = tar.entries.constFind(name);
            if (it == tar.entries.cend()) {
                if (ok) *ok = false;
                return {};
            }
            QFile f(filePath);
            if (!f.open(QIODevice::ReadOnly) || !f.seek(it->offset)) {
                if (ok) *ok = false;
                return {};
            }
            const QByteArray d = f.read(it->size);
            if (ok) *ok = d.size() == it->size;
            return d;
        }
        // 7-Zip and RAR were read into memory when the book opened.
        const auto it = files.constFind(QStringLiteral("archive/") + name);
        if (ok) *ok = it != files.cend();
        return it != files.cend() ? *it : QByteArray();
    }

    QByteArray resource(const QString& path, bool* ok) const override
    {
        const auto a = alias.constFind(path);
        if (a != alias.cend()) return entry(pages[*a], ok);
        return MemoryBook::resource(path, ok);
    }
    bool hasResource(const QString& path) const override { return alias.contains(path) || MemoryBook::hasResource(path); }
};

#ifdef HAVE_LIBARCHIVE
// Reads every entry of a 7-Zip or RAR archive (they can't be read one entry at a time cheaply).
bool readWithLibarchive(ComicBook& book, QStringList* names, QString* error)
{
    archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    const QByteArray native = QFile::encodeName(book.filePath);
    if (archive_read_open_filename(a, native.constData(), 1 << 16) != ARCHIVE_OK) {
        if (error) *error = QStringLiteral("The archive can't be opened: %1").arg(QString::fromLocal8Bit(archive_error_string(a)));
        archive_read_free(a);
        return false;
    }
    archive_entry* e = nullptr;
    bool encrypted = false;
    int r;
    while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
        if (archive_entry_filetype(e) != AE_IFREG) continue;
        if (archive_entry_is_encrypted(e)) {
            encrypted = true;
            continue;
        }
        const char* utf8 = archive_entry_pathname_utf8(e);
        const QString name = utf8 ? QString::fromUtf8(utf8) : QString::fromLocal8Bit(archive_entry_pathname(e));
        QByteArray data;
        char buf[65536];
        la_ssize_t n;
        while ((n = archive_read_data(a, buf, sizeof buf)) > 0) data.append(buf, n);
        if (n < 0) continue;   // a damaged entry: left out
        book.files.insert(QStringLiteral("archive/") + name, data);
        *names << name;
    }
    archive_read_free(a);
    if (names->isEmpty()) {
        if (error) *error = encrypted ? QStringLiteral("The archive is locked with a password, so its pages can't be shown.")
                                      : QStringLiteral("The archive is empty or damaged.");
        return false;
    }
    return true;
}
#endif

void readComicInfo(ComicBook& book, const QByteArray& xml)
{
    const auto doc = dom::parse(xml, dom::ParseMode::Xml);
    const dom::Node* root = doc->documentElement();
    if (!root) return;
    auto text = [&](const char* name) {
        for (const auto& c : root->children)
            if (c->isElement() && c->name.compare(QLatin1String(name), Qt::CaseInsensitive) == 0) return c->textContent().simplified();
        return QString();
    };
    const QString title = text("title"), series = text("series"), number = text("number");
    book.metadata.series = series;
    book.metadata.seriesNumber = number;
    if (!title.isEmpty() && !series.isEmpty() && !title.contains(series, Qt::CaseInsensitive))
        book.metadata.title = number.isEmpty() ? QStringLiteral("%1: %2").arg(series, title) : QStringLiteral("%1 #%2: %3").arg(series, number, title);
    else if (!title.isEmpty()) book.metadata.title = title;
    else if (!series.isEmpty()) book.metadata.title = number.isEmpty() ? series : QStringLiteral("%1 #%2").arg(series, number);
    for (const char* role : {"writer", "penciller", "artist"}) {
        for (const QString& a : text(role).split(QLatin1Char(','), Qt::SkipEmptyParts))
            if (!book.metadata.authors.contains(a.trimmed())) book.metadata.authors << a.trimmed();
    }
    book.metadata.language = text("languageiso");
    book.metadata.description = text("summary");
    book.metadata.publisher = text("publisher");
    const QString manga = text("manga").toLower();
    if (manga == QLatin1String("yesandrighttoleft")) book.metadata.rightToLeft = true;
}

}  // namespace

bool ComicFormat::canOpen(const QString& path, const QByteArray& head) const
{
    Q_UNUSED(head);   // the archive kind is read in open(): a ".cbr" that is a zip opens too
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("cbz") || s == QLatin1String("cbr") || s == QLatin1String("cb7") || s == QLatin1String("cbt");
}

std::shared_ptr<Book> ComicFormat::open(const QString& path, QString* error) const
{
    auto book = std::make_shared<ComicBook>();
    book->filePath = path;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The file can't be read: %1").arg(f.errorString());
        return nullptr;
    }
    const QByteArray head = f.read(1024);
    f.close();
    book->kind = comic::archiveKind(head);
    QStringList names;
    QString err;
    switch (book->kind) {
    case comic::Archive::Zip:
        if (!book->zip.open(path, &err)) {
            if (error) *error = err;
            return nullptr;
        }
        if (book->zip.hasEncryptedEntries()) {
            if (error) *error = QStringLiteral("The archive is locked with a password, so its pages can't be shown.");
            return nullptr;
        }
        book->zipOpen = true;
        names = book->zip.names();
        break;
    case comic::Archive::Tar:
        if (!book->tar.build(path, &err)) {
            if (error) *error = err;
            return nullptr;
        }
        names = book->tar.order;
        break;
    case comic::Archive::SevenZip:
    case comic::Archive::Rar:
#ifdef HAVE_LIBARCHIVE
        if (!readWithLibarchive(*book, &names, &err)) {
            if (error) *error = err;
            return nullptr;
        }
        break;
#else
        if (error)
            *error = book->kind == comic::Archive::Rar ? QStringLiteral("This comic is a RAR archive, which FreeReader can't open on this device yet. A CBZ copy opens.")
                                                       : QStringLiteral("This comic is a 7-Zip archive, which FreeReader can't open on this device yet. A CBZ copy opens.");
        return nullptr;
#endif
    case comic::Archive::Unknown:
        if (error) *error = QStringLiteral("This file isn't a comic archive (zip, RAR, 7-Zip or tar), or it is damaged.");
        return nullptr;
    }

    book->pages = comic::pageNames(names);
    if (book->pages.isEmpty()) {
        if (error) *error = QStringLiteral("The archive has no pictures to show.");
        return nullptr;
    }
    for (const QString& n : names)
        if (QFileInfo(n).fileName().compare(QLatin1String("ComicInfo.xml"), Qt::CaseInsensitive) == 0) {
            bool ok = false;
            const QByteArray xml = book->entry(n, &ok);
            if (ok) readComicInfo(*book, xml);
            break;
        }
    if (book->metadata.title.isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();
    book->metadata.pictures = true;

    // One chapter per page; a sub-folder that starts (a chapter of a volume) is an entry in the contents.
    QString folder;
    const bool folders = [&] {
        QSet<QString> dirs;
        for (const QString& p : book->pages) dirs.insert(QFileInfo(p).path());
        return dirs.size() > 1;
    }();
    for (int i = 0; i < book->pages.size(); ++i) {
        const QString suffix = QFileInfo(book->pages[i]).suffix().toLower();
        const QString img = QStringLiteral("img/%1.%2").arg(i + 1, 4, 10, QLatin1Char('0')).arg(suffix);
        book->alias.insert(img, i);
        const QString chapter = QStringLiteral("page%1.html").arg(i + 1, 4, 10, QLatin1Char('0'));
        const QString pageTitle = QStringLiteral("%1").arg(i + 1);
        book->addChapter(chapter, convert::imagePage(pageTitle, img, book->metadata.language));
        // Page numbers are the pictures' numbers (a comic has no text to count pages by).
        book->pageList.push_back({pageTitle, chapter});
        const QString dir = QFileInfo(book->pages[i]).path();
        if (folders && dir != folder) {
            folder = dir;
            book->addTocEntry(QFileInfo(dir).fileName().isEmpty() ? dir : QFileInfo(dir).fileName(), chapter, 0);
        }
        if (i == 0) book->metadata.coverPath = img;
    }
    return book;
}

}  // namespace core

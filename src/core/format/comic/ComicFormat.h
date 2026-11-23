// Comics, manga and graphic novels: CBZ (zip), CBT (tar), CB7 (7-Zip) and CBR (RAR) archives of page pictures.
// The archive kind is read from the bytes (many ".cbr" files are zips). Pages in natural order ("2" before
// "10"); ComicInfo.xml gives the title, series and number, writer, language, summary and whether it is manga
// read right to left. Sub-folders (a volume's chapters) become the contents. 7-Zip and RAR need libarchive
// (HAVE_LIBARCHIVE); without it they get a plain message.
#pragma once

#include "core/format/FormatPlugin.h"

namespace core {

class ComicFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("Comic"); }
    QStringList fileSuffixes() const override
    {
        return {QStringLiteral("cbz"), QStringLiteral("cbr"), QStringLiteral("cb7"), QStringLiteral("cbt")};
    }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

namespace comic {
enum class Archive { Unknown, Zip, Tar, SevenZip, Rar };
Archive archiveKind(const QByteArray& head);
bool canReadArchive(Archive kind);   // 7-Zip and RAR only with libarchive
// The pages of an archive whose entries are `names`: the pictures, in reading order (hidden files and
// __MACOSX left out).
QStringList pageNames(const QStringList& names);
}  // namespace comic

}  // namespace core

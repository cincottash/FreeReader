// Microsoft Compiled HTML Help (CHM): Windows help files, and some older e-books, through chmlib (HAVE_CHMLIB).
// The pages in the order of the file's contents (its .hhc), which also become the book's contents; pages,
// pictures and style sheets read from the file when they are shown. Pages that don't say their encoding are
// read in the one the file's language uses (Shift-JIS for Japanese, Windows-1251 for Russian…).
#pragma once

#include "core/format/FormatPlugin.h"

namespace core {

class ChmFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("CHM"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("chm")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

}  // namespace core

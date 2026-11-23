// DjVu (scanned books, common in libraries and archives) through DjVuLibre (HAVE_DJVU): shown page by page, each
// page drawn when it is shown, with the book's own bookmarks as the contents and its title and author when it
// names them. The hidden text layer (OCR), when there is one, tells the book's language. Without DjVuLibre the
// files get a plain message.
#pragma once

#include "core/format/FormatPlugin.h"

namespace core {

class DjvuFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("DjVu"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("djvu"), QStringLiteral("djv")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

}  // namespace core

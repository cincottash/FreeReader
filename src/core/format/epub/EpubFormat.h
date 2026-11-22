// EPUB 2 and 3 (reflowable). DRM-protected books are refused with a plain message; nothing is decrypted.
#pragma once

#include "core/format/FormatPlugin.h"

namespace core {

class EpubFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("EPUB"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("epub")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

}  // namespace core

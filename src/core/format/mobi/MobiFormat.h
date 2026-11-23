// Kindle books without DRM: MOBI (Mobipocket), AZW, AZW3 (KF8), PRC, and Palm's PDB text (PalmDOC). A book with
// both a MOBI and a KF8 part (what Kindle tools and Project Gutenberg make) opens its KF8 part: it keeps more of
// the layout. DRM-protected books are refused with a plain message; nothing is decrypted. KFX (Amazon's newest
// format) isn't supported.
#pragma once

#include "core/format/FormatPlugin.h"

namespace core {

class MobiFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("Kindle (MOBI, AZW3)"); }
    QStringList fileSuffixes() const override
    {
        return {QStringLiteral("mobi"), QStringLiteral("azw"), QStringLiteral("azw3"), QStringLiteral("prc"), QStringLiteral("pdb"), QStringLiteral("kfx")};
    }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

}  // namespace core

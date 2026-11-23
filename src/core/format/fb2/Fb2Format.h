// FictionBook 2 (FB2), also zipped (.fb2.zip, .fbz): an XML book with its pictures inside. Each top-level
// section is a chapter (nested sections are entries inside it); poems, epigraphs, citations and notes keep their
// shape; the notes body becomes a Notes chapter that footnote links lead to.
#pragma once

#include "core/format/FormatPlugin.h"

namespace core {

class Fb2Format : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("FB2"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("fb2"), QStringLiteral("fbz"), QStringLiteral("fb2.zip")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

}  // namespace core

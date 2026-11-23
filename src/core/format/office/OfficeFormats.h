// Documents from word processors: Word (DOCX), OpenDocument text (ODT, from LibreOffice and others) and RTF.
// Headings (their styles' outline levels) start chapters and fill the contents; bold, italics, links, pictures,
// tables, lists and footnotes are kept. Old Word files (.doc) aren't read: saving them as DOCX opens them.
#pragma once

#include "core/format/FormatPlugin.h"

namespace core {

class DocxFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("Word (DOCX)"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("docx"), QStringLiteral("docm"), QStringLiteral("dotx")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

class OdtFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("OpenDocument (ODT)"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("odt"), QStringLiteral("ott"), QStringLiteral("fodt")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

class RtfFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("RTF"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("rtf")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

// Old Word documents (.doc): a clear message (Word 97-2003 files are a different, binary format).
class DocFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("Word 97-2003 (DOC)"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("doc")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

}  // namespace core

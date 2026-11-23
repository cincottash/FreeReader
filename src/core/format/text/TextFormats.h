// Plain text (TXT), Markdown (MD) and calibre's TXTZ (a zip of the text, its pictures and metadata.opf).
// Text becomes paragraphs and chapters: chapter headings ("Capítulo I", "CHAPTER 2", a Roman numeral alone)
// start chapters and fill the contents; a poem's short lines keep their breaks.
#pragma once

#include "core/format/FormatPlugin.h"

namespace core {

class MemoryBook;

class TextFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("Text"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("txt"), QStringLiteral("text")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

class MarkdownFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("Markdown"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("md"), QStringLiteral("markdown")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

class TxtzFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("TXTZ"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("txtz")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

namespace text {
// Fills `book` with chapters made from plain text. `fileName` names the chapters' files.
void fromPlainText(MemoryBook& book, const QString& text);
// Fills `book` with chapters made from Markdown (images relative to the book's folder or zip).
void fromMarkdown(MemoryBook& book, const QString& text);
// Markdown's inline marks (emphasis, code, links, images) on one paragraph's text, as HTML.
QString markdownInline(const QString& text);
}  // namespace text

}  // namespace core

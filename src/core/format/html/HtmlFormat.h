// A web page as a book (HTML, HTM, XHTML), calibre's HTMLZ (a zip of index.html, its pictures and
// metadata.opf), and a plain ZIP that holds one book (Project Gutenberg's "-h.zip": a page and its pictures; a
// zipped TXT or FB2). A long page is split into chapters at its top-level headings; links between them keep
// working, and pictures come from the page's folder (or the zip).
#pragma once

#include "core/format/FormatPlugin.h"

namespace core {

class MemoryBook;

class HtmlFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("HTML"); }
    QStringList fileSuffixes() const override
    {
        return {QStringLiteral("html"), QStringLiteral("htm"), QStringLiteral("xhtml"), QStringLiteral("xht"), QStringLiteral("shtml")};
    }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

class HtmlzFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("HTMLZ"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("htmlz")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

// A plain ZIP that holds one book: a web page and its pictures (Project Gutenberg's "-h.zip"), a text or
// Markdown file, an FB2 book, or only pictures (a comic). An EPUB inside a zip isn't opened (unzip it first).
class ZipBookFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("ZIP"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("zip")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
};

namespace html {
// Splits the page `data` (read from `virtualPath` in the book) into chapters and adds them to `book`; reads the
// title, author and language from the page when the book doesn't have them yet.
void addPage(MemoryBook& book, const QByteArray& data, const QString& virtualPath);
// The encoding a page declares (BOM, <meta charset>, the XML declaration), as UTF-8 text.
QString decodePage(const QByteArray& data);
// Reads metadata.opf (calibre's HTMLZ, TXTZ) into the book's metadata.
void readOpfMetadata(MemoryBook& book, const QByteArray& opf);
}  // namespace html

}  // namespace core

// The common ground of the formats that aren't EPUB: each turns its file into HTML chapters held in memory
// (MemoryBook), so the reader, search, translation and notes work the same for every format.
#pragma once

#include "Book.h"
#include "core/zip/ZipArchive.h"

#include <QHash>
#include <QImage>

#include <functional>

namespace core {

// A book built in memory: chapters and images by path. Paths it doesn't hold can come from a zip (HTMLZ, a
// comic) or from the folder of the file (an HTML file's images), never from outside that folder.
class MemoryBook : public Book {
public:
    QHash<QString, QByteArray> files;
    ZipArchive zip;            // used when zipOpen
    bool zipOpen = false;
    QString diskRoot;          // a folder whose files (and subfolders) can be read, or empty

    QByteArray resource(const QString& path, bool* ok = nullptr) const override;
    bool hasResource(const QString& path) const override;

    void addFile(const QString& path, const QByteArray& data) { files.insert(path, data); }
    // A chapter: its HTML at `path`, added to the spine; with a title, also to the contents at `depth` (an
    // href with a #fragment adds a contents entry inside the chapter instead).
    void addChapter(const QString& path, const QByteArray& html, const QString& title = {}, int depth = 0);
    void addTocEntry(const QString& title, const QString& href, int depth);
};

namespace convert {

QString escape(const QString& text);   // & < > " for HTML text and attributes
// A whole HTML page around `body` (already HTML), as UTF-8.
QByteArray page(const QString& title, const QString& body, const QString& lang = {}, const QString& css = {});
// Text of an unknown encoding: a BOM (UTF-8, UTF-16), valid UTF-8, else Windows-1252 (which Latin-1 is part of).
QString decodeText(const QByteArray& data);
// Bytes in a named encoding ("utf-8", "windows-1252", "iso-8859-1", "utf-16le"…); unknown names fall back to
// decodeText.
QString decodeAs(const QByteArray& data, const QString& encoding);
// The language of a text from its most common words: "es", "en", "fr", "de", "it", "pt", or empty when unsure.
// For books that don't say their language (plain text, some PDFs), so translation knows what it reads.
QString guessLanguage(const QString& text);
// "image/jpeg", "image/png", "image/gif", "image/webp", "image/bmp", "image/svg+xml" or empty, from the bytes.
QString imageType(const QByteArray& data);
QString imageSuffix(const QString& mimeType);   // "jpg", "png"…
// Natural order for page files: "page2.jpg" before "page10.jpg".
bool naturalLess(const QString& a, const QString& b);
// An image page: the picture alone on its page, as large as fits (comics, scanned pages).
QByteArray imagePage(const QString& title, const QString& imageHref, const QString& lang = {});
// A PNG of an image, made smaller to at most `maxSide` pixels on its longer side (covers).
QByteArray coverPng(const QImage& image, int maxSide = 600);

// Paragraphs and headings of a document (Word, OpenDocument, RTF) gathered in order, then split into chapters:
// at the highest heading level used more than once, else into parts of about 60,000 characters.
class Chapters {
public:
    void heading(int level, const QString& html, const QString& plain, const QString& id = {});
    void block(const QString& html, int textLength = -1);
    void pageBreak();   // a hard page break in the document: a possible chapter start
    bool isEmpty() const { return m_items.empty(); }
    // Adds the chapters to the book ("partNNNN.html", or under `dir`), fixing "#id" links between them.
    void finish(MemoryBook& book, const QString& dir = {}) const;

private:
    struct Item {
        int level = 0;   // 0: a paragraph or other block; -1: a page break
        QString html, plain, id;
        int length = 0;
    };
    std::vector<Item> m_items;
};

}  // namespace convert
}  // namespace core

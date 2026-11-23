// PDF and XPS through MuPDF (HAVE_MUPDF). A PDF with text becomes reflowing text like any other book, so words
// can be selected, translated, highlighted and searched: paragraphs joined again across lines and pages, words
// split by a hyphen at a line's end put back together, running heads and page numbers left out, headings found
// from the type size, the PDF's own contents (outline) as the contents and chapters, its page numbers as the
// book's page numbers. Pictures stay where they are. A page without text (a cover, a full-page picture) is shown
// as the page itself. A scanned PDF (pictures of pages, no text) is shown page by page, like a comic.
// Without MuPDF the files get a plain message.
#pragma once

#include "core/format/FormatPlugin.h"

namespace core {

class PdfFormat : public FormatPlugin {
public:
    QString name() const override { return QStringLiteral("PDF"); }
    QStringList fileSuffixes() const override { return {QStringLiteral("pdf"), QStringLiteral("xps"), QStringLiteral("oxps")}; }
    bool canOpen(const QString& path, const QByteArray& head) const override;
    std::shared_ptr<Book> open(const QString& path, QString* error) const override;
    // The title, authors, language and cover only: a long PDF's text isn't read to add it to the library.
    std::shared_ptr<Book> openDetails(const QString& path, QString* error) const override;
};

namespace pdf {
bool available();   // built with MuPDF
}

}  // namespace core

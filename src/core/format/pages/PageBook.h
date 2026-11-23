// Books whose pages are pictures drawn from the file when they are shown (a scanned PDF, a DjVu book, a PDF page
// without text): one chapter per page, the page drawn at a size that fills a large window, the last few kept.
#pragma once

#include "core/format/Convert.h"

#include <QImage>
#include <QMutex>

#include <utility>
#include <vector>

namespace core {

class PageBook : public MemoryBook {
public:
    int pageCount = 0;

    // "pages/0001.png" for the first page; "pages/cover.png" draws the first page small.
    static QString pagePath(int index);
    static QString coverPath() { return QStringLiteral("pages/cover.png"); }
    // The HTML that shows page `index` (0-based) as a picture filling the reading area.
    static QString pagePicture(int index, const QString& alt = {});

    // Every page a chapter of its own, numbered with `labels` (the book's own page numbers, or empty for 1, 2,
    // 3…); marks the book as pictures and its cover as the first page.
    void addPicturePages(const QStringList& labels);

    QByteArray resource(const QString& path, bool* ok = nullptr) const override;
    bool hasResource(const QString& path) const override;

protected:
    // Draws page `index` with its longer side `longSide` pixels long. Called from any thread (one at a time).
    virtual QImage renderPage(int index, int longSide) const = 0;

private:
    int pageIndexOf(const QString& path) const;   // -1 when it isn't a page picture, -2 for the cover

    mutable QMutex m_cacheMutex;
    mutable std::vector<std::pair<int, QByteArray>> m_cache;   // most recent last
};

}  // namespace core

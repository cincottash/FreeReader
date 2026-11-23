#include "PageBook.h"

#include <QBuffer>
#include <QMutexLocker>

namespace core {

namespace {
constexpr int kPageLongSide = 2200;   // sharp in a full-screen window; drawn again only when not kept
constexpr int kCoverLongSide = 600;
constexpr size_t kKeptPages = 4;
}  // namespace

QString PageBook::pagePath(int index) { return QStringLiteral("pages/%1.png").arg(index + 1, 4, 10, QLatin1Char('0')); }

QString PageBook::pagePicture(int index, const QString& alt)
{
    return QStringLiteral("<div><img data-page-image=\"1\" src=\"%1\" alt=\"%2\"/></div>").arg(pagePath(index), convert::escape(alt));
}

void PageBook::addPicturePages(const QStringList& labels)
{
    metadata.pictures = true;
    metadata.coverPath = coverPath();
    for (int i = 0; i < pageCount; ++i) {
        const QString label = i < labels.size() && !labels[i].isEmpty() ? labels[i] : QString::number(i + 1);
        const QString chapter = QStringLiteral("page%1.html").arg(i + 1, 4, 10, QLatin1Char('0'));
        addChapter(chapter, convert::page(label, pagePicture(i, label), metadata.language));
        pageList.push_back({label, chapter});
    }
}

int PageBook::pageIndexOf(const QString& path) const
{
    if (!path.startsWith(QLatin1String("pages/"))) return -1;
    if (path == coverPath()) return pageCount > 0 ? -2 : -1;
    if (!path.endsWith(QLatin1String(".png"))) return -1;
    bool ok = false;
    const int n = path.mid(6, path.size() - 10).toInt(&ok);
    return ok && n >= 1 && n <= pageCount ? n - 1 : -1;
}

bool PageBook::hasResource(const QString& path) const { return pageIndexOf(path) != -1 || MemoryBook::hasResource(path); }

QByteArray PageBook::resource(const QString& path, bool* ok) const
{
    const int index = pageIndexOf(path);
    if (index == -1) return MemoryBook::resource(path, ok);
    const int key = index == -2 ? -1 : index;
    {
        QMutexLocker lock(&m_cacheMutex);
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (it->first == key) {
                auto kept = std::move(*it);
                m_cache.erase(it);
                m_cache.push_back(std::move(kept));
                if (ok) *ok = true;
                return m_cache.back().second;
            }
    }
    const QImage img = renderPage(index == -2 ? 0 : index, index == -2 ? kCoverLongSide : kPageLongSide);
    if (img.isNull()) {
        if (ok) *ok = false;
        return {};
    }
    QByteArray png;
    QBuffer out(&png);
    out.open(QIODevice::WriteOnly);
    img.save(&out, "PNG", 40);   // some compression: quick to make, and small enough to keep a few
    QMutexLocker lock(&m_cacheMutex);
    m_cache.emplace_back(key, png);
    if (m_cache.size() > kKeptPages) m_cache.erase(m_cache.begin());
    if (ok) *ok = true;
    return png;
}

}  // namespace core

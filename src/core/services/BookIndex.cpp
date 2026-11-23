#include "BookIndex.h"

#include "core/format/Book.h"
#include "core/layout/Flow.h"

#include <QLocale>
#include <QObject>

#include <algorithm>
#include <cmath>

namespace core {

std::shared_ptr<BookIndex> buildIndex(const Book& book)
{
    auto ix = std::make_shared<BookIndex>();
    const size_t n = book.spine.size();
    ix->chapterLength.resize(n);
    ix->chapterStart.resize(n);
    ix->ids.resize(n);
    layout::FlowOptions opt;   // no fonts: only the text and ids are needed here
    qint64 g = 0;
    for (size_t i = 0; i < n; ++i) {
        const auto flow = layout::buildFlow(book, int(i), opt);
        ix->chapterStart[i] = g;
        ix->chapterLength[i] = flow->length;
        ix->ids[i] = flow->ids;
        g += flow->length;
    }
    ix->total = g;

    ix->toc.resize(book.toc.size());
    for (size_t i = 0; i < book.toc.size(); ++i) {
        Position p;
        if (ix->positionFor(book, book.toc[i].href, &p)) ix->toc[i] = {p.spine, p.offset, ix->global(p)};
    }

    // Printed page numbers, when the book lists enough of them to be useful (a comic's are its pictures).
    ix->pictures = book.metadata.pictures;
    if (book.pageList.size() >= 5 || (book.metadata.pictures && !book.pageList.empty())) {
        for (const PageListEntry& e : book.pageList) {
            Position p;
            if (ix->positionFor(book, e.href, &p)) ix->printedPages.push_back({ix->global(p), e.label});
        }
        std::stable_sort(ix->printedPages.begin(), ix->printedPages.end(),
                         [](const BookIndex::PrintedPage& a, const BookIndex::PrintedPage& b) { return a.global < b.global; });
        if (ix->printedPages.size() < book.pageList.size() / 2) ix->printedPages.clear();   // mostly unresolved: unreliable
    }
    return ix;
}

qint64 BookIndex::global(const Position& p) const
{
    if (p.spine < 0 || p.spine >= int(chapterStart.size())) return 0;
    return chapterStart[size_t(p.spine)] + qBound(0, p.offset, chapterLength[size_t(p.spine)]);
}

Position BookIndex::fromGlobal(qint64 g) const
{
    if (chapterStart.empty()) return {};
    g = qBound<qint64>(0, g, qMax<qint64>(0, total - 1));
    auto it = std::upper_bound(chapterStart.begin(), chapterStart.end(), g);
    const int spine = int(std::distance(chapterStart.begin(), it)) - 1;
    return {qMax(0, spine), int(g - chapterStart[size_t(qMax(0, spine))])};
}

bool BookIndex::positionFor(const Book& book, const QString& href, Position* out) const
{
    const qsizetype hash = href.indexOf(QLatin1Char('#'));
    const QString path = hash >= 0 ? href.left(hash) : href;
    const QString frag = hash >= 0 ? href.mid(hash + 1) : QString();
    const int spine = book.spineIndexOf(path);
    if (spine < 0 || spine >= int(ids.size())) return false;
    out->spine = spine;
    out->offset = frag.isEmpty() ? 0 : ids[size_t(spine)].value(frag, 0);
    return true;
}

int BookIndex::pageCount() const
{
    if (!printedPages.empty()) return int(printedPages.size());
    return int(qMax<qint64>(1, (total + kCharsPerPage - 1) / kCharsPerPage));
}

QString BookIndex::pageLabel(const Position& p) const
{
    const qint64 g = global(p);
    if (!printedPages.empty()) {
        auto it = std::upper_bound(printedPages.begin(), printedPages.end(), g,
                                   [](qint64 v, const PrintedPage& pp) { return v < pp.global; });
        if (it == printedPages.begin()) return printedPages.front().label;
        return std::prev(it)->label;
    }
    return QLocale().toString(qMin<qint64>(g / kCharsPerPage + 1, pageCount()));
}

int BookIndex::pageNumber(const Position& p) const { return pageNumberAt(global(p)); }

int BookIndex::pageNumberAt(qint64 g) const
{
    if (!printedPages.empty()) {
        auto it = std::upper_bound(printedPages.begin(), printedPages.end(), g,
                                   [](qint64 v, const PrintedPage& pp) { return v < pp.global; });
        return qMax(1, int(it - printedPages.begin()));
    }
    return int(qMin<qint64>(g / kCharsPerPage + 1, pageCount()));
}

QString BookIndex::lastPageLabel() const
{
    if (!printedPages.empty()) return printedPages.back().label;
    return QLocale().toString(pageCount());
}

Position BookIndex::positionOfPage(int pageNumber) const
{
    pageNumber = qBound(1, pageNumber, pageCount());
    if (!printedPages.empty()) return fromGlobal(printedPages[size_t(pageNumber - 1)].global);
    return fromGlobal(qint64(pageNumber - 1) * kCharsPerPage);
}

int BookIndex::tocEntryAt(const Position& p) const
{
    const qint64 g = global(p);
    int best = -1;
    qint64 bestG = -1;
    for (size_t i = 0; i < toc.size(); ++i) {
        if (toc[i].spine < 0) continue;
        if (toc[i].global <= g && toc[i].global >= bestG) {
            best = int(i);
            bestG = toc[i].global;
        }
    }
    return best;
}

QString chapterLeftText(const BookIndex& ix, const Position& p, int readingSpeed, bool pages)
{
    const qint64 g = ix.global(p);
    qint64 next = ix.total;
    for (const auto& t : ix.toc)
        if (t.spine >= 0 && t.global > g && t.global < next) next = t.global;
    // A comic has no text to time: always its pages. A book without chapters counts to its end.
    const bool wholeBook = next == ix.total && std::none_of(ix.toc.begin(), ix.toc.end(), [&](const auto& t) { return t.spine >= 0 && t.global <= g; });
    if (pages || ix.pictures) {
        const int left = qMax(0, ix.pageNumberAt(qMax(g, next - 1)) - ix.pageNumber(p));
        if (wholeBook) {
            if (left == 0) return QObject::tr("Last page");
            if (left == 1) return QObject::tr("1 page left");
            return QObject::tr("%1 pages left").arg(QLocale().toString(left));
        }
        if (left == 0) return QObject::tr("Last page of the chapter");
        if (left == 1) return QObject::tr("1 page left in chapter");
        return QObject::tr("%1 pages left in chapter").arg(QLocale().toString(left));
    }
    const double minutes = double(next - g) / double(qMax(1, readingSpeed));
    if (minutes < 1) return QObject::tr("Less than 1 min left in chapter");
    const int m = int(std::lround(minutes));
    if (m < 60) return QObject::tr("%1 min left in chapter").arg(QLocale().toString(m));
    return QObject::tr("%1 h %2 min left in chapter").arg(QLocale().toString(m / 60)).arg(m % 60);
}

}  // namespace core

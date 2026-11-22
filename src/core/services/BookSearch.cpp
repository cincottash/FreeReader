#include "BookSearch.h"

#include "core/format/Book.h"
#include "core/layout/Flow.h"

namespace core {

QString foldForSearch(const QString& s, bool matchCase, bool matchAccents)
{
    QString out = s;
    for (qsizetype i = 0; i < out.size(); ++i) {
        QChar c = out[i];
        if (!matchAccents && c.unicode() >= 0xC0 && !c.isSurrogate()) {
            const QString d = QString(c).normalized(QString::NormalizationForm_D);
            if (!d.isEmpty() && d[0].unicode() < 0x250) c = d[0];   // á -> a, ñ -> n; keeps one character
        }
        if (!matchCase) c = c.toLower();
        // Typographic apostrophes and quotes match plain ones.
        if (c == QChar(0x2019) || c == QChar(0x2018)) c = QLatin1Char('\'');
        out[i] = c;
    }
    return out;
}

QList<SearchHit> searchBook(const Book& book, const QString& query, const SearchOptions& o, const std::atomic<bool>* cancel, bool* truncated)
{
    QList<SearchHit> hits;
    if (truncated) *truncated = false;
    const QString needle = foldForSearch(query.simplified(), o.matchCase, o.matchAccents);
    if (needle.isEmpty()) return hits;
    layout::FlowOptions opt;   // text only
    for (int spine = 0; spine < int(book.spine.size()); ++spine) {
        if (cancel && cancel->load()) break;
        const auto flow = layout::buildFlow(book, spine, opt);
        for (const layout::Block& b : flow->blocks) {
            if (b.kind != layout::Block::Text || b.text.isEmpty()) continue;
            // Line breaks inside a paragraph (<br>) count as spaces.
            QString text = b.text;
            text.replace(QChar(0x2028), QLatin1Char(' '));
            text.replace(QChar(0x00AD), QChar(0x200B));   // soft hyphens never appear in a search
            const QString hay = foldForSearch(text, o.matchCase, o.matchAccents);
            qsizetype at = 0;
            while ((at = hay.indexOf(needle, at)) >= 0) {
                const qsizetype end = at + needle.size();
                const bool wordOk = !o.wholeWord || ((at == 0 || !hay[at - 1].isLetterOrNumber()) && (end >= hay.size() || !hay[end].isLetterOrNumber()));
                if (wordOk) {
                    SearchHit h;
                    h.spine = spine;
                    h.start = b.start + int(at);
                    h.end = b.start + int(end);
                    const qsizetype from = qMax<qsizetype>(0, at - 40);
                    h.before = text.mid(from, at - from);
                    h.match = text.mid(at, needle.size());
                    h.after = text.mid(end, 40);
                    // Whole words at the edges of the context.
                    if (from > 0) {
                        const qsizetype sp = h.before.indexOf(QLatin1Char(' '));
                        if (sp >= 0) h.before = QStringLiteral("…") + h.before.mid(sp + 1);
                    }
                    if (end + 40 < text.size()) {
                        const qsizetype sp = h.after.lastIndexOf(QLatin1Char(' '));
                        if (sp > 0) h.after = h.after.left(sp) + QStringLiteral("…");
                    }
                    h.before.remove(QChar(0x200B));
                    h.after.remove(QChar(0x200B));
                    hits << h;
                    if (hits.size() >= o.limit) {
                        if (truncated) *truncated = true;
                        return hits;
                    }
                }
                at = end;
            }
        }
    }
    return hits;
}

}  // namespace core

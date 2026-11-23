#include "Highlighting.h"

#include "Annotations.h"
#include "BookEngine.h"
#include "BookIndex.h"

#include "core/layout/Flow.h"
#include "core/layout/Layout.h"

#include <QStringList>

namespace core {

QString vocabularyText(const QString& highlighted)
{
    QString text = highlighted.simplified();
    while (!text.isEmpty() && !text.front().isLetterOrNumber()) text.remove(0, 1);
    while (!text.isEmpty() && !text.back().isLetterOrNumber()) text.chop(1);
    int letters = 0;
    for (const QChar c : text)
        if (c.isLetter()) ++letters;
    if (letters < 2 || text.size() > 60 || text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() > 4) return {};
    return text;
}

QString contextAround(const QString& sentence, const QString& text)
{
    QString context = sentence.simplified();
    if (context.size() <= 600) return context;
    const int at = int(context.indexOf(text, 0, Qt::CaseInsensitive));
    const int from = at < 0 ? 0 : qMax(0, at - 250);
    return (from > 0 ? QStringLiteral("…") : QString()) + context.mid(from, 500).trimmed() +
           (from + 500 < context.size() ? QStringLiteral("…") : QString());
}

void wholeWords(BookEngine& engine, int spine, int* from, int* to)
{
    const auto cl = engine.layout(spine);
    if (!cl) return;
    for (const layout::Block& b : cl->flow->blocks) {
        if (b.kind != layout::Block::Text) continue;
        auto isWord = [&](int i) { return i >= 0 && i < int(b.text.size()) && (b.text[i].isLetterOrNumber() || b.text[i] == QChar(0x00AD)); };
        if (*from >= b.start && *from < b.start + b.length) {
            int i = *from - b.start;
            while (i > 0 && isWord(i) && isWord(i - 1)) --i;
            *from = b.start + i;
        }
        if (*to > b.start && *to <= b.start + b.length) {
            int i = *to - b.start;
            while (i < int(b.text.size()) && isWord(i) && isWord(i - 1)) ++i;
            *to = b.start + i;
        }
    }
}

void firstWord(BookEngine& engine, int spine, int start, int end, int* from, int* to)
{
    *from = start;
    *to = end;
    wholeWords(engine, spine, from, to);
    const auto cl = engine.layout(spine);
    if (!cl) return;
    for (const layout::Block& b : cl->flow->blocks) {
        if (b.kind != layout::Block::Text || *from < b.start || *from >= b.start + b.length) continue;
        int i = *from - b.start;
        while (i < int(b.text.size()) && !b.text[i].isLetterOrNumber()) ++i;   // punctuation before it
        int e = i;
        while (e < int(b.text.size()) && (b.text[e].isLetterOrNumber() || b.text[e] == QChar(0x00AD))) ++e;
        if (e > i) {
            *from = b.start + i;
            *to = b.start + e;
        }
        return;
    }
}

HighlightChange highlightRange(Annotations& notes, BookEngine& engine, int bookId, int spine, int start, int end,
                               const QString& color, const QString& chapter, const QString& selectedText)
{
    HighlightChange change;
    if (!bookId || end <= start) return change;
    // Inside an existing highlight: that one changes color (a highlight is never stacked on another). Its own
    // color again takes the highlight away, as in Kindle (the owner's request, 2026-11-22). A note on it goes
    // with it only when it has none: one with a note is kept (Remove Highlight still removes it).
    for (Annotation a : notes.highlightsAt(bookId, spine, start, end))
        if (a.start <= start && a.end >= end) {
            change.from = a.start;
            change.to = a.end;
            change.text = a.text;
            if (a.color == color && a.note.trimmed().isEmpty()) {
                notes.remove(a.id);
                change.removed = true;
                return change;
            }
            a.color = color;
            notes.update(a);
            change.id = a.id;
            change.recolored = true;
            return change;
        }
    // Whole words, as in Kindle: a selection that starts or ends inside a word takes the whole word.
    int from = start, to = end;
    wholeWords(engine, spine, &from, &to);
    // Overlapping ones are merged into the new highlight, keeping their notes.
    QStringList kept;
    for (const Annotation& a : notes.highlightsAt(bookId, spine, from, to)) {
        from = qMin(from, a.start);
        to = qMax(to, a.end);
        if (!a.note.trimmed().isEmpty()) kept << a.note.trimmed();
        notes.remove(a.id);
    }
    const auto cl = engine.layout(spine);
    Annotation a;
    a.bookId = bookId;
    a.kind = Annotation::Highlight;
    a.spine = spine;
    a.start = from;
    a.end = to;
    a.text = cl ? cl->flow->textRange(from, to).simplified() : selectedText.simplified();
    a.color = color;
    a.note = kept.join(QStringLiteral("\n\n"));
    a.cfi = engine.cfiFor({spine, from});
    if (const auto ix = engine.index()) a.pageLabel = ix->pageLabel({spine, from});
    a.chapter = chapter;
    change.id = notes.add(a);
    change.from = from;
    change.to = to;
    change.text = a.text;
    return change;
}

}  // namespace core

#include "KindleApply.h"

#include "Annotations.h"
#include "BookIndex.h"
#include "Library.h"
#include "WordLog.h"

#include "core/format/Book.h"
#include "core/layout/Flow.h"
#include "core/position/Position.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSqlDatabase>
#include <QTextBoundaryFinder>

#include <map>

namespace core {

namespace {
QString tr(const char* text) { return QCoreApplication::translate("ReaderView", text); }
}  // namespace

KindleResult applyKindlePlan(const KindlePlan& plan, const KindleOptions& options, const KindleTarget& t)
{
    KindleResult res;
    if (!t.book || !t.bookId || !t.notes) return res;
    const Book& book = *t.book;
    std::map<int, std::shared_ptr<const layout::ChapterFlow>> flows;
    auto flowOf = [&](int spine) {
        auto it = flows.find(spine);
        if (it == flows.end()) it = flows.emplace(spine, layout::buildFlow(book, spine, layout::FlowOptions())).first;
        return it->second;
    };
    auto chapterTitleAt = [&](const Position& p) { return t.chapterTitle ? t.chapterTitle(p) : QString(); };
    auto pageLabelAt = [&](const Position& p) { return t.index ? t.index->pageLabel(p) : QString(); };
    // The sentence around a place, for the Word List.
    auto sentenceAt = [&](const layout::ChapterFlow& flow, int start, int end) -> QString {
        for (const layout::Block& b : flow.blocks) {
            if (b.kind != layout::Block::Text || start < b.start || start >= b.start + b.length) continue;
            QTextBoundaryFinder f(QTextBoundaryFinder::Sentence, b.text);
            f.setPosition(start - b.start);
            int s = f.isAtBoundary() ? start - b.start : int(f.toPreviousBoundary());
            if (s < 0) s = 0;
            f.setPosition(qMin(int(b.text.size()), qMax(end - b.start, s + 1)));
            int e = f.isAtBoundary() ? f.position() : int(f.toNextBoundary());
            if (e < 0) e = int(b.text.size());
            QString sentence = QString(b.text.mid(s, e - s)).simplified();
            if (sentence.size() > 600) sentence = sentence.left(600) + QStringLiteral("…");
            return sentence;
        }
        return {};
    };
    auto isShort = [](QString text) {
        text = text.simplified();
        while (!text.isEmpty() && !text.front().isLetterOrNumber()) text.remove(0, 1);
        while (!text.isEmpty() && !text.back().isLetterOrNumber()) text.chop(1);
        int letters = 0;
        for (const QChar c : text)
            if (c.isLetter()) ++letters;
        return letters >= 2 && text.size() <= 60 && text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() <= 4 ? text : QString();
    };
    auto meaningOf = [&](const QString& word) { return t.offlineMeaning ? t.offlineMeaning(word) : TranslationResult(); };
    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto placeAt = [&](int spine, int start, int end, const QString& sentence) {
        WordPlace place;
        place.bookId = t.bookId;
        place.bookTitle = t.bookTitle;
        place.language = t.language;
        place.spine = spine;
        place.start = start;
        place.end = end;
        place.sentence = sentence;
        place.cfi = makeCfi(spine, *flowOf(spine), start);
        place.pageLabel = pageLabelAt({spine, start});
        place.chapter = chapterTitleAt({spine, start});
        return place;
    };
    // One transaction for the whole import (thousands of rows).
    QSqlDatabase db = t.library ? QSqlDatabase::database(t.library->connectionName()) : QSqlDatabase();
    if (db.isValid()) db.transaction();
    for (const KindlePlaced& p : plan.items) {
        const KindleItem& it = p.item;
        if (it.bookmark) {
            if (!options.bookmarks || p.approx.spine < 0) continue;
            const auto flow = flowOf(p.approx.spine);
            if (t.notes->bookmarkAt(t.bookId, p.approx.spine, p.approx.offset - 200, p.approx.offset + 200)) {
                ++res.skipped;
                continue;
            }
            Annotation a;
            a.bookId = t.bookId;
            a.kind = Annotation::Bookmark;
            a.spine = p.approx.spine;
            a.start = a.end = p.approx.offset;
            a.text = flow->textRange(p.approx.offset, p.approx.offset + 80).simplified();
            a.note = it.note;
            a.cfi = makeCfi(a.spine, *flow, a.start);
            a.chapter = chapterTitleAt(p.approx);
            a.pageLabel = pageLabelAt(p.approx);
            if (t.notes->add(a)) ++res.bookmarks;
            continue;
        }
        const QString word = isShort(it.text);
        // An equivalent: this edition's words for a highlight it words differently (orange, noted with the Kindle words).
        const bool equivalent = !p.found && p.placed() && options.equivalents;
        if (p.found || equivalent) {
            const auto flow = flowOf(p.spine);
            const QString sentence = sentenceAt(*flow, p.start, p.end);
            const QString bookWords = flow->textRange(p.start, p.end).simplified();
            if (options.highlights) {
                if (!t.notes->highlightsAt(t.bookId, p.spine, p.start, p.end).isEmpty()) {
                    ++res.skipped;
                } else {
                    Annotation a;
                    a.bookId = t.bookId;
                    a.kind = Annotation::Highlight;
                    a.spine = p.spine;
                    a.start = p.start;
                    a.end = p.end;
                    a.text = bookWords;
                    // Its own color from a Biblioglot export; yellow from a Kindle; orange on an equivalent.
                    a.color = !p.found                                    ? QStringLiteral("orange")
                              : Annotations::colors().contains(it.color) ? it.color
                                                                          : QStringLiteral("yellow");
                    a.note = p.found ? it.note
                                     : (plan.notebook ? tr("Imported: «%1»") : tr("Kindle: «%1»")).arg(it.text.simplified()) +
                                           (it.note.isEmpty() ? QString() : QStringLiteral("\n\n") + it.note);
                    a.cfi = makeCfi(p.spine, *flow, p.start);
                    a.chapter = chapterTitleAt({p.spine, p.start});
                    a.pageLabel = pageLabelAt({p.spine, p.start});
                    if (t.notes->add(a)) ++(p.found ? res.highlights : res.equivalents);
                }
            }
            if (t.words && !word.isEmpty() && (p.found ? options.words : options.missingWords)) {
                // The word as highlighted on the Kindle, with this edition's sentence around its equivalent.
                TranslationResult meaning = meaningOf(word);
                meaning.text = word;
                if (!p.found)
                    meaning.note = tr("From your Kindle edition (location %1); this edition says «%2» here.").arg(it.location).arg(bookWords) +
                                   (it.note.isEmpty() ? QString() : QStringLiteral("\n\n") + it.note);
                const WordPlace place = placeAt(p.spine, p.start, p.end, sentence);
                if (t.words->mark(meaning, place, now)) ++(p.found ? res.words : res.missingWords);
            }
        } else {
            const QString from = tr("From your Kindle edition (location %1); this edition words it differently.").arg(it.location);
            if (options.missingWords && t.words && !word.isEmpty()) {
                // Not in this edition's text: kept for practice, at about the place it would be.
                TranslationResult meaning = meaningOf(word);
                meaning.text = word;
                meaning.note = it.note.isEmpty() ? from : it.note + QStringLiteral("\n\n") + from;
                const WordPlace place = placeAt(p.approx.spine, p.approx.offset, p.approx.offset, QString());
                if (t.words->mark(meaning, place, now)) ++res.missingWords;
            }
            // A passage, or a highlight with a note, that this edition doesn't have: a bookmark at about its place
            // with the Kindle text and the note, so nothing is lost.
            if (options.missingPassages && (word.isEmpty() || !it.note.isEmpty()) && p.approx.spine >= 0) {
                const QString quoted = QStringLiteral("«%1»").arg(it.text.simplified());
                bool already = false;
                for (const Annotation& a : t.notes->inChapter(t.bookId, p.approx.spine))
                    if (a.kind == Annotation::Bookmark && a.text == quoted) already = true;
                if (already) {
                    ++res.skipped;
                } else {
                    const auto flow = flowOf(p.approx.spine);
                    Annotation a;
                    a.bookId = t.bookId;
                    a.kind = Annotation::Bookmark;
                    a.spine = p.approx.spine;
                    a.start = a.end = p.approx.offset;
                    a.text = quoted;
                    a.note = it.note.isEmpty() ? from : it.note + QStringLiteral("\n\n") + from;
                    a.cfi = makeCfi(a.spine, *flow, a.start);
                    a.chapter = chapterTitleAt(p.approx);
                    a.pageLabel = pageLabelAt(p.approx);
                    if (t.notes->add(a)) ++res.passages;
                }
            }
        }
    }
    if (db.isValid()) db.commit();
    return res;
}

}  // namespace core

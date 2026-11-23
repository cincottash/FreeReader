#include "KindleImport.h"

#include "core/format/Book.h"
#include "core/layout/Flow.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <vector>

namespace core {

namespace {

bool isBookmarkNote(const QString& note)
{
    const QString n = note.trimmed().toLower();
    return n == QLatin1String("bookmark") || n == QLatin1String("marcador");
}

KindleExport parseBookcision(const QJsonObject& o)
{
    KindleExport ex;
    ex.title = o.value(QLatin1String("title")).toString();
    ex.authors = o.value(QLatin1String("authors")).toString();
    for (const QJsonValue& v : o.value(QLatin1String("highlights")).toArray()) {
        const QJsonObject h = v.toObject();
        KindleItem it;
        it.text = h.value(QLatin1String("text")).toString().trimmed();
        it.note = h.value(QLatin1String("note")).toString().trimmed();
        it.location = h.value(QLatin1String("location")).toObject().value(QLatin1String("value")).toInt();
        it.bookmark = h.value(QLatin1String("isNoteOnly")).toBool() ? (it.note.isEmpty() || isBookmarkNote(it.note)) : isBookmarkNote(it.note);
        if (it.bookmark && isBookmarkNote(it.note)) it.note.clear();
        if (h.value(QLatin1String("isNoteOnly")).toBool() && !it.bookmark) {
            // A note on its own: kept as a bookmark with the note.
            it.bookmark = true;
        }
        if (it.text.isEmpty() && !it.bookmark) continue;
        ex.items << it;
    }
    if (ex.items.isEmpty()) ex.error = QStringLiteral("The file has no highlights.");
    return ex;
}

// "My Clippings.txt": entries separated by "==========": the book's title line, a details line ("- Your Highlight
// on page 3 | Location 142-143 | Added on ..."; Spanish Kindles say "subrayado", "marcador", "nota", "posición"),
// a blank line and the text. A note follows its highlight at the same location.
KindleExport parseClippings(const QString& all, const QString& titleHint)
{
    KindleExport ex;
    static const QRegularExpression locRe(QStringLiteral("(?:location|loc\\.|posici[oó]n)\\s+(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    const QString hint = titleHint.left(20).toLower();
    for (const QString& entry : all.split(QStringLiteral("=========="))) {
        QStringList lines = entry.split(QLatin1Char('\n'));
        for (QString& l : lines) l = l.trimmed().remove(QChar(0xFEFF));
        while (!lines.isEmpty() && lines.first().isEmpty()) lines.removeFirst();
        if (lines.size() < 2) continue;
        const QString title = lines[0];
        if (!hint.isEmpty() && !title.toLower().startsWith(hint)) continue;
        const QString details = lines[1].toLower();
        const auto m = locRe.match(details);
        if (!m.hasMatch()) continue;
        const QString text = lines.mid(2).join(QLatin1Char(' ')).simplified();
        KindleItem it;
        it.location = m.captured(1).toInt();
        if (ex.title.isEmpty()) ex.title = title;
        if (details.contains(QLatin1String("bookmark")) || details.contains(QLatin1String("marcador"))) {
            it.bookmark = true;
        } else if (details.contains(QLatin1String("note")) || details.contains(QLatin1String("nota"))) {
            // Attach to the highlight at this location.
            bool attached = false;
            for (int i = int(ex.items.size()) - 1; i >= 0 && !attached; --i)
                if (ex.items[i].location == it.location && !ex.items[i].bookmark) {
                    if (isBookmarkNote(text)) ex.items[i].bookmark = true;
                    else ex.items[i].note = text;
                    attached = true;
                }
            if (attached) continue;
            it.bookmark = true;
            it.note = text;
        } else {
            it.text = text;
        }
        if (it.text.isEmpty() && !it.bookmark) continue;
        ex.items << it;
    }
    if (ex.items.isEmpty()) ex.error = titleHint.isEmpty() ? QStringLiteral("The file has no highlights.")
                                                           : QStringLiteral("The file has no highlights for this book.");
    return ex;
}

// The book as one normalized string (lower case, one kind of dash and quote, single spaces, no soft hyphens) with
// each character's place in the book.
struct BookText {
    QString text;
    std::vector<int> spine, offset;
};

QChar normChar(QChar c)
{
    switch (c.unicode()) {
    case 0x2014: case 0x2013: case 0x2012: case 0x2015: return QLatin1Char('-');
    case 0x00AB: case 0x00BB: case 0x201C: case 0x201D: case 0x201E: case 0x2018: case 0x2019: case 0x201A: case 0x0022: case 0x0027:
        return QLatin1Char(' ');
    case 0x2028: case 0x00A0: case 0x202F: case 0x2009: return QLatin1Char(' ');
    default: return c.toLower();
    }
}

QString normalize(const QString& s)
{
    QString out;
    out.reserve(s.size());
    const QString n = s.normalized(QString::NormalizationForm_C);
    for (const QChar c : n) {
        if (c == QChar(0x00AD) || c == QChar(0x200B)) continue;
        const QChar m = normChar(c);
        if (m.isSpace()) {
            if (!out.isEmpty() && out.back() != QLatin1Char(' ')) out += QLatin1Char(' ');
        } else {
            out += m;
        }
    }
    return out.trimmed();
}

// The text to look for: normalized, without the punctuation at its ends ("—bostezó" -> "bostezó", "tonto," -> "tonto").
QString keyFor(const QString& text)
{
    QString k = normalize(text);
    while (!k.isEmpty() && !k.front().isLetterOrNumber()) k.remove(0, 1);
    while (!k.isEmpty() && !k.back().isLetterOrNumber()) k.chop(1);
    return k;
}

BookText bookText(const Book& book, const std::atomic<bool>* cancel)
{
    BookText bt;
    layout::FlowOptions opt;   // text only
    for (int spine = 0; spine < int(book.spine.size()); ++spine) {
        if (cancel && cancel->load()) break;
        const auto flow = layout::buildFlow(book, spine, opt);
        for (const layout::Block& b : flow->blocks) {
            if (b.kind != layout::Block::Text || b.text.isEmpty()) continue;
            const QString t = b.text;   // already NFC from the parser in practice
            for (int i = 0; i < int(t.size()); ++i) {
                const QChar c = t[i];
                if (c == QChar(0x00AD) || c == QChar(0x200B)) continue;
                const QChar m = normChar(c);
                if (m.isSpace()) {
                    if (bt.text.isEmpty() || bt.text.back() == QLatin1Char(' ')) continue;
                    bt.text += QLatin1Char(' ');
                } else {
                    bt.text += m;
                }
                bt.spine.push_back(spine);
                bt.offset.push_back(b.start + i);
            }
            if (!bt.text.isEmpty() && bt.text.back() != QLatin1Char(' ')) {
                bt.text += QLatin1Char(' ');   // paragraphs are separate
                bt.spine.push_back(spine);
                bt.offset.push_back(b.start + int(t.size()));
            }
        }
    }
    return bt;
}

std::vector<int> occurrences(const QString& hay, const QString& key)
{
    std::vector<int> out;
    if (key.isEmpty()) return out;
    qsizetype at = 0;
    while ((at = hay.indexOf(key, at)) >= 0) {
        const qsizetype end = at + key.size();
        const bool startOk = at == 0 || !hay[at - 1].isLetterOrNumber();
        const bool endOk = end >= hay.size() || !hay[end].isLetterOrNumber();
        if (startOk && endOk) out.push_back(int(at));
        at += 1;
    }
    return out;
}

}  // namespace

KindleExport parseKindleExport(const QByteArray& data, const QString& titleHint)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error == QJsonParseError::NoError && doc.isObject() && doc.object().contains(QLatin1String("highlights")))
        return parseBookcision(doc.object());
    const QString text = QString::fromUtf8(data);
    if (text.contains(QStringLiteral("==========")))
        return parseClippings(text, titleHint);
    KindleExport ex;
    ex.error = QStringLiteral("That isn't a Kindle highlights export (Bookcision's JSON or My Clippings.txt).");
    return ex;
}

KindlePlan planKindleImport(const Book& book, const KindleExport& ex, const std::atomic<bool>* cancel)
{
    KindlePlan plan;
    const BookText bt = bookText(book, cancel);
    const int total = int(bt.text.size());
    if (total == 0) return plan;

    struct Work {
        QString key;
        std::vector<int> occ;
    };
    std::vector<Work> work(size_t(ex.items.size()));
    for (int i = 0; i < int(ex.items.size()); ++i) {
        if (cancel && cancel->load()) return plan;
        const KindleItem& it = ex.items[i];
        if (it.text.isEmpty()) continue;
        work[size_t(i)].key = keyFor(it.text);
        work[size_t(i)].occ = occurrences(bt.text, work[size_t(i)].key);
    }

    // Anchors: texts of 4+ characters found exactly once, kept only where they agree with each other (the longest
    // run in which both the Kindle location and the book position go forward).
    std::vector<std::pair<int, int>> cand;   // (location, book position)
    for (int i = 0; i < int(ex.items.size()); ++i)
        if (work[size_t(i)].key.size() >= 4 && work[size_t(i)].occ.size() == 1)
            cand.push_back({ex.items[i].location, work[size_t(i)].occ[0]});
    std::sort(cand.begin(), cand.end());
    std::vector<int> tails, tailIdx, prev(cand.size(), -1);
    for (int i = 0; i < int(cand.size()); ++i) {
        const int pos = cand[size_t(i)].second;
        const auto it = std::upper_bound(tails.begin(), tails.end(), pos);
        const int k = int(it - tails.begin());
        if (it == tails.end()) {
            tails.push_back(pos);
            tailIdx.push_back(i);
        } else {
            *it = pos;
            tailIdx[size_t(k)] = i;
        }
        prev[size_t(i)] = k > 0 ? tailIdx[size_t(k - 1)] : -1;
    }
    std::vector<std::pair<int, int>> anchors;
    for (int i = tailIdx.empty() ? -1 : tailIdx.back(); i >= 0; i = prev[size_t(i)]) anchors.push_back(cand[size_t(i)]);
    std::reverse(anchors.begin(), anchors.end());
    plan.anchors = int(anchors.size());

    // Characters per Kindle location, from the first and last anchors (about 110 for prose); without anchors, the
    // export's last location is taken as the end of the book.
    int maxLoc = 1;
    for (const KindleItem& it : ex.items) maxLoc = qMax(maxLoc, it.location);
    double slope = double(total) / double(maxLoc), offset = 0;
    if (anchors.size() >= 2 && anchors.back().first > anchors.front().first) {
        slope = double(anchors.back().second - anchors.front().second) / double(anchors.back().first - anchors.front().first);
        offset = anchors.front().second - slope * anchors.front().first;
    }
    auto predict = [&](int loc) -> double {
        if (anchors.size() >= 2) {
            const auto it = std::lower_bound(anchors.begin(), anchors.end(), std::make_pair(loc, -1));
            if (it != anchors.begin() && it != anchors.end()) {
                const auto& a = *(it - 1);
                const auto& b = *it;
                if (b.first != a.first) return a.second + double(b.second - a.second) * double(loc - a.first) / double(b.first - a.first);
                return a.second;
            }
        }
        return slope * loc + offset;
    };
    auto posAt = [&](int at) -> Position {
        at = qBound(0, at, total - 1);
        return Position{bt.spine[size_t(at)], bt.offset[size_t(at)]};
    };
    // A highlight is accepted within about 12 locations of where it should be.
    const double window = qMax(1200.0, 12 * slope);

    for (int i = 0; i < int(ex.items.size()); ++i) {
        const KindleItem& it = ex.items[i];
        KindlePlaced p;
        p.item = it;
        const double expected = predict(it.location);
        p.approx = posAt(int(std::lround(expected)));
        const Work& w = work[size_t(i)];
        if (!w.occ.empty()) {
            const int best = *std::min_element(w.occ.begin(), w.occ.end(), [&](int a, int b) { return std::abs(a - expected) < std::abs(b - expected); });
            if (std::abs(best - expected) <= window) {
                const Position s = posAt(best), e = posAt(best + int(w.key.size()) - 1);
                p.found = s.spine == e.spine;
                if (p.found) {
                    p.spine = s.spine;
                    p.start = s.offset;
                    p.end = e.offset + 1;
                    p.approx = s;
                }
            }
        }
        if (it.bookmark) ++plan.bookmarks;
        else if (p.found) ++plan.found;
        else ++plan.notFound;
        if (!it.note.isEmpty()) ++plan.notes;
        if (it.location >= plan.lastLocation) {
            plan.lastLocation = it.location;
            plan.lastRead = p.approx;
            plan.lastFraction = qBound(0.0, expected / total, 1.0);
        }
        plan.items << p;
    }
    return plan;
}

}  // namespace core

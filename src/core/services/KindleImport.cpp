#include "KindleImport.h"

#include "core/format/Book.h"
#include "core/layout/Flow.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
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
    std::vector<char> upper;   // the character was upper case in the book (names)
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
                if (c == QChar(0x2028)) {   // a line break inside a paragraph (an address): a break like a paragraph's end
                    if (bt.text.isEmpty() || bt.text.back() == QLatin1Char('\n')) continue;
                    if (bt.text.back() == QLatin1Char(' ')) {
                        bt.text.back() = QLatin1Char('\n');
                        continue;
                    }
                    bt.text += QLatin1Char('\n');
                    bt.spine.push_back(spine);
                    bt.offset.push_back(b.start + i);
                    bt.upper.push_back(0);
                    continue;
                }
                const QChar m = normChar(c);
                if (m.isSpace()) {
                    if (bt.text.isEmpty() || bt.text.back() == QLatin1Char(' ') || bt.text.back() == QLatin1Char('\n')) continue;
                    bt.text += QLatin1Char(' ');
                } else {
                    bt.text += m;
                }
                bt.spine.push_back(spine);
                bt.offset.push_back(b.start + i);
                bt.upper.push_back(c.isUpper() ? 1 : 0);
            }
            if (!bt.text.isEmpty() && bt.text.back() == QLatin1Char(' ')) {
                bt.text.back() = QLatin1Char('\n');   // the end of a paragraph (or a line of an address), kept
            } else if (!bt.text.isEmpty() && bt.text.back() != QLatin1Char('\n')) {
                bt.text += QLatin1Char('\n');   // paragraphs are separate
                bt.spine.push_back(spine);
                bt.offset.push_back(b.start + int(t.size()));
                bt.upper.push_back(0);
            }
        }
    }
    return bt;
}

// Short words ("de", "the", "un", "of") are passed over when choosing an equivalent, in any language: a
// highlight is far more often a word that means something.
bool isSmallWord(const QString& w) { return w.size() < 4; }

// Letters without their accents ("caminó" -> "camino"), for comparing word forms.
QString foldAccents(const QString& s)
{
    QString out;
    for (const QChar c : s.normalized(QString::NormalizationForm_D))
        if (c.category() != QChar::Mark_NonSpacing) out += c;
    return out;
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

// ---- FreeReader's own Notebook exports (Markdown, JSON, CSV, TSV, plain text), read back.

const QStringList kColumns = {QStringLiteral("kind"), QStringLiteral("color"), QStringLiteral("text"), QStringLiteral("note"),
                              QStringLiteral("page"), QStringLiteral("chapter"), QStringLiteral("cfi"), QStringLiteral("created"),
                              QStringLiteral("book")};

KindleItem itemFrom(const NotebookRow& r)
{
    KindleItem it;
    it.bookmark = r.bookmark;
    it.text = r.text.trimmed();
    it.note = r.note.trimmed();
    it.color = r.color.trimmed().toLower();
    it.cfi = r.cfi.trimmed();
    // A bookmark keeps the first words of its page, cut at a length: without the last (perhaps cut) word, they're
    // found again in the book.
    if (it.bookmark) {
        QStringList words = it.text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.size() > 3) words.removeLast();
        it.text = words.join(QLatin1Char(' '));
    }
    return it;
}

KindleExport finishNotebook(KindleExport ex)
{
    ex.notebook = true;
    // In the file's order (reading order): the order stands in for a place until the CFIs are read with the book.
    for (int i = 0; i < int(ex.items.size()); ++i) ex.items[i].location = i + 1;
    if (ex.items.isEmpty()) ex.error = QStringLiteral("The file has no highlights, notes or bookmarks.");
    return ex;
}

KindleExport parseNotebookJson(const QJsonObject& o)
{
    KindleExport ex;
    const QJsonObject book = o.value(QLatin1String("book")).toObject();
    ex.title = book.value(QLatin1String("title")).toString();
    ex.authors = book.value(QLatin1String("author")).toString();
    for (const QJsonValue& v : o.value(QLatin1String("annotations")).toArray()) {
        const QJsonObject a = v.toObject();
        NotebookRow r;
        r.bookmark = a.value(QLatin1String("kind")).toString() == QLatin1String("bookmark");
        r.text = a.value(QLatin1String("text")).toString();
        r.note = a.value(QLatin1String("note")).toString();
        r.color = a.value(QLatin1String("color")).toString();
        r.cfi = a.value(QLatin1String("cfi")).toString();
        if (r.text.trimmed().isEmpty() && !r.bookmark) continue;
        ex.items << itemFrom(r);
    }
    return finishNotebook(ex);
}

// RFC 4180: fields in double quotes when they hold the separator, a quote or a line break; a quote doubled. The
// separator is a comma, or a semicolon (Excel in many European languages saves CSV so).
QList<QStringList> readCsv(const QString& s, QChar sep = QLatin1Char(','))
{
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false, any = false;
    for (int i = 0; i < int(s.size()); ++i) {
        const QChar c = s[i];
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < s.size() && s[i + 1] == QLatin1Char('"')) {
                    field += c;
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == QLatin1Char('"')) {
            quoted = true;
            any = true;
        } else if (c == sep) {
            row << field;
            field.clear();
            any = true;
        } else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (c == QLatin1Char('\r') && i + 1 < s.size() && s[i + 1] == QLatin1Char('\n')) ++i;
            if (any || !field.isEmpty()) rows << (row << field);
            row.clear();
            field.clear();
            any = false;
        } else {
            field += c;
            any = true;
        }
    }
    if (any || !field.isEmpty()) rows << (row << field);
    return rows;
}

QString unescapeTsv(const QString& f)
{
    QString out;
    for (int i = 0; i < int(f.size()); ++i) {
        if (f[i] == QLatin1Char('\\') && i + 1 < f.size()) {
            const QChar n = f[++i];
            out += n == QLatin1Char('t') ? QChar('\t') : n == QLatin1Char('n') ? QChar('\n') : n == QLatin1Char('r') ? QChar('\r') : n;
        } else {
            out += f[i];
        }
    }
    return out;
}

KindleExport parseNotebookTable(const QList<QStringList>& rows)
{
    KindleExport ex;
    if (rows.isEmpty()) return finishNotebook(ex);
    QHash<QString, int> col;
    for (int i = 0; i < int(rows[0].size()); ++i) col.insert(rows[0][i].trimmed().toLower(), i);
    auto get = [&](const QStringList& r, const char* name) {
        const int i = col.value(QLatin1String(name), -1);
        return i >= 0 && i < r.size() ? r[i] : QString();
    };
    for (int k = 1; k < int(rows.size()); ++k) {
        const QStringList& r = rows[k];
        NotebookRow n;
        n.bookmark = get(r, "kind").trimmed().toLower() == QLatin1String("bookmark");
        n.color = get(r, "color");
        n.text = get(r, "text");
        n.note = get(r, "note");
        n.cfi = get(r, "cfi");
        if (ex.title.isEmpty()) ex.title = get(r, "book");
        if (n.text.trimmed().isEmpty() && !n.bookmark) continue;
        ex.items << itemFrom(n);
    }
    return finishNotebook(ex);
}

// The plain text export (notebook::text below).
KindleExport parseNotebookText(const QString& all)
{
    KindleExport ex;
    const QStringList lines = all.split(QLatin1Char('\n'));
    NotebookRow cur;
    bool open = false, inNote = false, haveText = false;
    auto flush = [&] {
        if (open && (!cur.text.trimmed().isEmpty() || cur.bookmark)) ex.items << itemFrom(cur);
        cur = NotebookRow();
        open = inNote = haveText = false;
    };
    for (QString l : lines) {
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
        if (l.startsWith(QLatin1String("Title: ")) && !open) ex.title = l.mid(7).trimmed();
        else if (l.startsWith(QLatin1String("Author: ")) && !open) ex.authors = l.mid(8).trimmed();
        else if (l.startsWith(QStringLiteral("Highlight · ")) || l.startsWith(QStringLiteral("Bookmark · "))) {
            flush();
            open = true;
            const QStringList parts = l.split(QStringLiteral(" · "));
            cur.bookmark = parts[0] == QLatin1String("Bookmark");
            if (!cur.bookmark && parts.size() > 1) cur.color = parts[1];
        } else if (!open) {
            continue;
        } else if (l.trimmed().isEmpty()) {
            flush();
        } else if (l.startsWith(QLatin1String("Note: "))) {
            cur.note = l.mid(6);
            inNote = true;
        } else if (inNote && l.startsWith(QLatin1String("    "))) {
            cur.note += QLatin1Char('\n') + l.mid(4);
        } else if (!haveText) {
            cur.text = l.trimmed();
            haveText = true;
        }
    }
    flush();
    return finishNotebook(ex);
}

// The Markdown export (Annotations::markdown): "> quote", "*Page 12 · yellow*", the note's lines, and under
// "## Bookmarks" lines like "- Page 30 (Chapter): «first words»".
KindleExport parseNotebookMarkdown(const QString& all)
{
    KindleExport ex;
    static const QRegularExpression pageRe(QStringLiteral("^\\*Page [^·]*·\\s*(\\w+)\\*$"));
    static const QRegularExpression markRe(QStringLiteral("^- Page .*?«(.*)»\\s*$"));
    NotebookRow cur;
    bool open = false, bookmarks = false, afterMeta = false;
    auto flush = [&] {
        if (open && !cur.text.trimmed().isEmpty()) {
            cur.note = cur.note.trimmed();
            ex.items << itemFrom(cur);
        }
        cur = NotebookRow();
        open = afterMeta = false;
    };
    bool first = true;
    for (QString l : all.split(QLatin1Char('\n'))) {
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
        if (first && l.startsWith(QLatin1String("# "))) {
            ex.title = l.mid(2).trimmed();
            first = false;
            continue;
        }
        first = false;
        if (l.startsWith(QLatin1String("## "))) {
            flush();
            bookmarks = l.mid(3).trimmed() == QLatin1String("Bookmarks");
            continue;
        }
        if (bookmarks) {
            const auto m = markRe.match(l);
            if (m.hasMatch()) {
                NotebookRow b;
                b.bookmark = true;
                b.text = m.captured(1);
                ex.items << itemFrom(b);
            }
            continue;
        }
        if (l.startsWith(QLatin1String("> "))) {
            flush();
            open = true;
            cur.text = l.mid(2);
            continue;
        }
        if (!open) continue;
        const auto m = pageRe.match(l.trimmed());
        if (m.hasMatch() && !afterMeta) {
            cur.color = m.captured(1);
            afterMeta = true;
        } else if (afterMeta) {
            cur.note += l + QLatin1Char('\n');
        }
    }
    flush();
    return finishNotebook(ex);
}

QString csvField(const QString& f)
{
    if (!f.contains(QLatin1Char(',')) && !f.contains(QLatin1Char('"')) && !f.contains(QLatin1Char('\n')) && !f.contains(QLatin1Char('\r')))
        return f;
    QString q = f;
    q.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

QString tsvField(QString f)
{
    f.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    f.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    f.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    f.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    return f;
}

QStringList rowFields(const QString& title, const NotebookRow& r)
{
    return {r.bookmark ? QStringLiteral("bookmark") : QStringLiteral("highlight"), r.bookmark ? QString() : r.color, r.text, r.note,
            r.page, r.chapter, r.cfi, r.created, title};
}

}  // namespace

namespace notebook {

QString csv(const QString& title, const QList<NotebookRow>& rows)
{
    QString out = kColumns.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    for (const NotebookRow& r : rows) {
        QStringList f;
        for (const QString& x : rowFields(title, r)) f << csvField(x);
        out += f.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }
    return out;
}

QString tsv(const QString& title, const QList<NotebookRow>& rows)
{
    QString out = kColumns.join(QLatin1Char('\t')) + QLatin1Char('\n');
    for (const NotebookRow& r : rows) {
        QStringList f;
        for (const QString& x : rowFields(title, r)) f << tsvField(x);
        out += f.join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    return out;
}

QString text(const QString& title, const QString& author, const QList<NotebookRow>& rows)
{
    QString out = QStringLiteral("FreeReader Notebook\n");
    if (!title.isEmpty()) out += QStringLiteral("Title: %1\n").arg(title);
    if (!author.isEmpty()) out += QStringLiteral("Author: %1\n").arg(author);
    for (const NotebookRow& r : rows) {
        QStringList head{r.bookmark ? QStringLiteral("Bookmark") : QStringLiteral("Highlight")};
        if (!r.bookmark) head << r.color;
        head << QStringLiteral("Page %1").arg(r.page);
        if (!r.chapter.isEmpty()) head << r.chapter.simplified();
        out += QLatin1Char('\n') + head.join(QStringLiteral(" · ")) + QLatin1Char('\n');
        out += r.text.simplified() + QLatin1Char('\n');
        const QStringList note = r.note.trimmed().split(QLatin1Char('\n'));
        if (!r.note.trimmed().isEmpty()) {
            out += QStringLiteral("Note: ") + note[0] + QLatin1Char('\n');
            for (int i = 1; i < int(note.size()); ++i) out += QStringLiteral("    ") + note[i] + QLatin1Char('\n');
        }
    }
    return out;
}

}  // namespace notebook

KindleExport parseKindleExport(const QByteArray& data, const QString& titleHint)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error == QJsonParseError::NoError && doc.isObject() && doc.object().contains(QLatin1String("highlights")))
        return parseBookcision(doc.object());
    if (err.error == QJsonParseError::NoError && doc.isObject() && doc.object().contains(QLatin1String("annotations")))
        return parseNotebookJson(doc.object());
    QString text = QString::fromUtf8(data);
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);
    // A table: its first line names the columns, "kind" and "text" among them (in any order: a spreadsheet may move
    // or add columns), with commas, semicolons or tabs between them.
    const QString head = text.section(QLatin1Char('\n'), 0, 0).trimmed().toLower().remove(QLatin1Char('"'));
    auto names = [&](QChar sep) {
        QStringList cols;
        for (const QString& c : head.split(sep)) cols << c.trimmed();
        return cols.contains(QLatin1String("kind")) && cols.contains(QLatin1String("text"));
    };
    if (names(QLatin1Char(',')) && !head.contains(QLatin1Char('\t')))
        return parseNotebookTable(readCsv(text));
    if (names(QLatin1Char(';')) && !head.contains(QLatin1Char('\t')))
        return parseNotebookTable(readCsv(text, QLatin1Char(';')));
    if (names(QLatin1Char('\t'))) {
        QList<QStringList> rows;
        for (QString line : text.split(QLatin1Char('\n'))) {
            if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
            if (line.isEmpty()) continue;
            QStringList f;
            for (const QString& x : line.split(QLatin1Char('\t'))) f << unescapeTsv(x);
            rows << f;
        }
        return parseNotebookTable(rows);
    }
    if (text.startsWith(QLatin1String("FreeReader Notebook")))
        return parseNotebookText(text);
    if (text.contains(QStringLiteral("==========")))
        return parseClippings(text, titleHint);
    if (text.startsWith(QLatin1String("# ")) && text.contains(QLatin1String("\n> ")))
        return parseNotebookMarkdown(text);
    KindleExport ex;
    ex.error = QStringLiteral("That isn't a highlights export FreeReader knows: a Kindle's (Bookcision's JSON or My Clippings.txt) "
                              "or a FreeReader Notebook export (Markdown, JSON, CSV, TSV or text).");
    return ex;
}

KindlePlan planKindleImport(const Book& book, const KindleExport& exIn, const std::atomic<bool>* cancel, bool equivalents)
{
    KindlePlan plan;
    plan.notebook = exIn.notebook;
    const BookText bt = bookText(book, cancel);
    const int total = int(bt.text.size());
    if (total == 0) return plan;
    // The place in the normalized text of a (chapter, offset): the first character at or after it.
    auto indexOf = [&](int spine, int offset) {
        int lo = 0, hi = total;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (bt.spine[size_t(mid)] < spine || (bt.spine[size_t(mid)] == spine && bt.offset[size_t(mid)] < offset)) lo = mid + 1;
            else hi = mid;
        }
        return qMin(lo, total - 1);
    };
    // A FreeReader export of this edition carries each item's CFI: its place gives a location like a Kindle's (about
    // 110 characters each), so repeated words are told apart. Only when every item has one (else the file's order).
    KindleExport ex = exIn;
    std::vector<int> cfiAt(size_t(ex.items.size()), -1);   // the exact place, when the CFI resolves in this book
    if (ex.notebook) {
        std::map<int, std::shared_ptr<const layout::ChapterFlow>> flows;
        bool all = !ex.items.isEmpty();
        for (int i = 0; i < int(ex.items.size()); ++i) {
            int spine = -1, raw = 0;
            QString path;
            if (!ex.items[i].cfi.isEmpty() && parseCfi(ex.items[i].cfi, &spine, &path, &raw) && spine >= 0 && spine < int(book.spine.size())) {
                auto it = flows.find(spine);
                if (it == flows.end()) it = flows.emplace(spine, layout::buildFlow(book, spine, layout::FlowOptions())).first;
                const int off = resolveCfiPath(*it->second, path, raw);
                if (off >= 0) cfiAt[size_t(i)] = indexOf(spine, off);
            }
            if (cfiAt[size_t(i)] < 0) all = false;
        }
        if (all)
            for (int i = 0; i < int(ex.items.size()); ++i) ex.items[i].location = cfiAt[size_t(i)] / 110 + 1;
    }
    // Highlights are looked for across paragraph breaks too (a long highlight can span two paragraphs).
    QString searchText = bt.text;
    searchText.replace(QLatin1Char('\n'), QLatin1Char(' '));

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
        work[size_t(i)].occ = occurrences(searchText, work[size_t(i)].key);
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

    std::vector<int> gStart(size_t(ex.items.size()), -1), gEnd(size_t(ex.items.size()), -1);   // book positions of the found ones
    for (int i = 0; i < int(ex.items.size()); ++i) {
        const KindleItem& it = ex.items[i];
        KindlePlaced p;
        p.item = it;
        // A FreeReader export's own place, when its CFI resolves in this book: exactly there (a short word like "de"
        // is in the book thousands of times; with few anchors the predicted place was far off, and a Notebook read
        // back into its own book got a second highlight elsewhere, 2026-11-27). Else predicted from the anchors.
        const double expected = ex.notebook && cfiAt[size_t(i)] >= 0 ? double(cfiAt[size_t(i)]) : predict(it.location);
        p.approx = posAt(int(std::lround(expected)));
        const Work& w = work[size_t(i)];
        if (!w.occ.empty()) {
            const int best = *std::min_element(w.occ.begin(), w.occ.end(), [&](int a, int b) { return std::abs(a - expected) < std::abs(b - expected); });
            if (std::abs(best - expected) <= window) {
                const Position s = posAt(best), e = posAt(best + int(w.key.size()) - 1);
                p.found = s.spine == e.spine;
                if (p.found) {
                    p.match = KindlePlaced::Exact;
                    p.spine = s.spine;
                    p.start = s.offset;
                    p.end = e.offset + 1;
                    p.approx = s;
                    gStart[size_t(i)] = best;
                    gEnd[size_t(i)] = best + int(w.key.size());
                }
            }
        }
        // A FreeReader bookmark goes back exactly where its CFI says, when it resolves in this book.
        if (it.bookmark && ex.notebook && cfiAt[size_t(i)] >= 0 && !p.found) p.approx = posAt(cfiAt[size_t(i)]);
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
    if (ex.notebook) {   // a Notebook export has no reading place to continue from
        plan.lastLocation = 0;
        plan.lastFraction = 0;
    }
    if (!equivalents || plan.notFound == 0) return plan;

    // ---- Equivalents for the highlights this edition words differently. Each lies between the highlights around
    // it that were found (usually a line or two apart): there, the same word in another form, else the word at the
    // matching spot. Nothing here depends on the book's language.
    struct Found {
        int loc, start, end;
    };
    std::vector<Found> found;
    std::vector<char> taken(size_t(total), 0);
    for (int i = 0; i < int(ex.items.size()); ++i)
        if (gStart[size_t(i)] >= 0) {
            found.push_back({ex.items[i].location, gStart[size_t(i)], gEnd[size_t(i)]});
            std::fill(taken.begin() + gStart[size_t(i)], taken.begin() + qMin(total, gEnd[size_t(i)]), char(1));
        }
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.loc != b.loc ? a.loc < b.loc : a.start < b.start; });
    // Every word of the book: [start, end) in the normalized text.
    std::vector<std::pair<int, int>> words;
    for (int i = 0; i < total;) {
        if (!bt.text[i].isLetterOrNumber()) {
            ++i;
            continue;
        }
        int j = i;
        while (j < total && (bt.text[j].isLetterOrNumber())) ++j;
        words.push_back({i, j});
        i = j;
    }
    // A name in the middle of a sentence ("Dumbledore"): never a stand-in for another word.
    auto isName = [&](int at) {
        if (!bt.upper[size_t(at)]) return false;
        int q = at - 1;
        while (q >= 0 && bt.text[q] == QLatin1Char(' ')) --q;
        if (q < 0 || bt.text[q] == QLatin1Char('\n')) return false;   // the start of a paragraph
        const QChar c = bt.text[q];
        return !(c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?') || c == QLatin1Char(':') || c == QLatin1Char('-') ||
                 c == QChar(0x00BF) || c == QChar(0x00A1) || c == QLatin1Char(';'));
    };
    // A sentence ends between two words.
    auto sentenceBreak = [&](int from, int to) {
        for (int q = from; q < to; ++q) {
            const QChar c = bt.text[q];
            if (c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?') || c == QLatin1Char(';') || c == QLatin1Char(':') ||
                c == QLatin1Char('\n'))
                return true;
        }
        return false;
    };
    auto commonPrefix = [](const QString& a, const QString& b) {
        int n = 0;
        while (n < a.size() && n < b.size() && a[n] == b[n]) ++n;
        return n;
    };

    for (int i = 0; i < int(plan.items.size()); ++i) {
        if (cancel && cancel->load()) break;
        KindlePlaced& p = plan.items[i];
        if (p.found || p.item.bookmark || p.item.text.isEmpty()) continue;
        const int loc = p.item.location;
        // The stretch: after the last highlight found before this location, up to the first one found after it.
        const auto after = std::upper_bound(found.begin(), found.end(), loc, [](int l, const Found& f) { return l < f.loc; });
        const auto before = std::lower_bound(found.begin(), found.end(), loc, [](const Found& f, int l) { return f.loc < l; });
        const double expected = predict(loc);
        int lo = before != found.begin() ? (before - 1)->end : int(expected) - 300;
        int hi = after != found.end() ? after->start : int(expected) + 300;
        double at = expected;
        if (before != found.begin() && after != found.end()) {
            const Found& a = *(before - 1);
            const Found& b = *after;
            at = b.loc > a.loc ? a.end + double(b.start - a.end) * double(loc - a.loc) / double(b.loc - a.loc) : (a.end + b.start) / 2.0;
        }
        if (hi < lo) std::swap(lo, hi);
        if (hi - lo < 80) {
            lo = int(at) - 40;
            hi = int(at) + 40;
        }
        lo = qBound(0, lo, total);
        hi = qBound(0, hi, total);
        // The Kindle words, and their content words.
        const QString key = keyFor(p.item.text);
        QStringList kWords;
        static const QRegularExpression nonWord(QStringLiteral("[^\\p{L}\\p{N}]+"), QRegularExpression::UseUnicodePropertiesOption);
        for (const QString& w : key.split(nonWord, Qt::SkipEmptyParts)) kWords << w;
        if (kWords.isEmpty()) continue;
        // Candidates: the stretch's free words that mean something.
        const auto first = std::lower_bound(words.begin(), words.end(), std::make_pair(lo, 0));
        const auto last = std::lower_bound(words.begin(), words.end(), std::make_pair(hi, 0));
        int bestIdx = -1, bestK = 0;
        double bestScore = 0;
        KindlePlaced::Match bestMatch = KindlePlaced::None;
        const double span = qMax(1, hi - lo);
        for (auto w = first; w != last; ++w) {
            if (taken[size_t(w->first)]) continue;
            const QString cand = bt.text.mid(w->first, w->second - w->first);
            if (isSmallWord(cand) || cand.size() < 2 || isName(w->first)) continue;
            const double near = 1.0 - qMin(1.0, std::abs(w->first - at) / span) * 0.5;
            for (int k = 0; k < kWords.size(); ++k) {
                const QString& kw = kWords[k];
                if (isSmallWord(kw)) continue;
                double score = 0;
                KindlePlaced::Match m = KindlePlaced::None;
                // The same word in another form: a long shared beginning, accents aside ("taco"/"tacón", "comparados"/"comparadas").
                const QString fa = foldAccents(cand), fb = foldAccents(kw);
                const int pre = commonPrefix(fa, fb);
                const int shorter = int(qMin(fa.size(), fb.size()));
                if (shorter >= 4 && pre >= qMin(5, shorter - 1) && pre >= 3) {
                    score = 2.0 + double(pre) / double(qMax(fa.size(), fb.size()));
                    m = KindlePlaced::ByForm;
                }
                if (m != KindlePlaced::None && score * near > bestScore) {
                    bestScore = score * near;
                    bestIdx = int(w - words.begin());
                    bestK = k;
                    bestMatch = m;
                }
            }
        }
        if (bestIdx < 0) {
            // The word at the matching spot.
            double bestDist = 1e18;
            // Passes: a word that means something; then any word but a name; then any word.
            for (int pass = 0; pass < 3 && bestIdx < 0; ++pass)
                for (auto w = first; w != last; ++w) {
                    if (taken[size_t(w->first)]) continue;
                    const QString cand = bt.text.mid(w->first, w->second - w->first);
                    if (pass == 0 && (isSmallWord(cand) || cand.size() < 3 || isName(w->first))) continue;
                    if (pass == 1 && isName(w->first)) continue;
                    const double d = std::abs(w->first - at);
                    if (d < bestDist) {
                        bestDist = d;
                        bestIdx = int(w - words.begin());
                    }
                }
            bestK = 0;
            bestMatch = KindlePlaced::ByPosition;
        }
        if (bestIdx < 0) continue;
        // As many words as the Kindle highlight had, lined up on the matched one, inside the stretch.
        // Never across the end of a sentence.
        const int lowest = int(first - words.begin()), highest = int(last - words.begin()) - 1;
        int a = bestIdx, b = bestIdx;
        while (a > lowest && bestIdx - a < bestK && !sentenceBreak(words[size_t(a - 1)].second, words[size_t(a)].first)) --a;
        while (b < highest && b - a + 1 < int(kWords.size()) && !sentenceBreak(words[size_t(b)].second, words[size_t(b + 1)].first)) ++b;
        auto spanFree = [&](int x, int y) {
            for (int q = words[size_t(x)].first; q < words[size_t(y)].second; ++q)
                if (taken[size_t(q)]) return false;
            return true;
        };
        while ((a < bestIdx || b > bestIdx) && !spanFree(a, b)) {
            if (a < bestIdx) ++a;
            else --b;
        }
        const int gs = words[size_t(a)].first, ge = words[size_t(b)].second;
        Position s = posAt(gs), e = posAt(ge - 1);
        if (s.spine != e.spine) {
            s = posAt(words[size_t(bestIdx)].first);
            e = posAt(words[size_t(bestIdx)].second - 1);
            if (s.spine != e.spine) continue;
        }
        std::fill(taken.begin() + gs, taken.begin() + ge, char(1));
        p.match = bestMatch;
        p.equivalent = bt.text.mid(gs, ge - gs);
        p.spine = s.spine;
        p.start = s.offset;
        p.end = e.offset + 1;
        p.approx = s;
        if (bestMatch == KindlePlaced::ByForm) ++plan.byForm;
        else ++plan.byPosition;
    }
    return plan;
}

}  // namespace core

#include "Annotations.h"

#include "Library.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

namespace core {

namespace {

QString iso(const QDateTime& t) { return t.isValid() ? t.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral(""); }
QVariant text(const QString& s) { return s.isNull() ? QVariant(QStringLiteral("")) : QVariant(s); }

const char* kColumns = "SELECT id, book_id, kind, spine, sel_start, sel_end, cfi, text, color, note, page_label, chapter, created, modified FROM annotations ";

Annotation read(const QSqlQuery& q)
{
    Annotation a;
    a.id = q.value(0).toInt();
    a.bookId = q.value(1).toInt();
    a.kind = Annotation::Kind(q.value(2).toInt());
    a.spine = q.value(3).toInt();
    a.start = q.value(4).toInt();
    a.end = q.value(5).toInt();
    a.cfi = q.value(6).toString();
    a.text = q.value(7).toString();
    a.color = q.value(8).toString();
    a.note = q.value(9).toString();
    a.pageLabel = q.value(10).toString();
    a.chapter = q.value(11).toString();
    a.created = QDateTime::fromString(q.value(12).toString(), Qt::ISODateWithMs);
    a.modified = QDateTime::fromString(q.value(13).toString(), Qt::ISODateWithMs);
    return a;
}

}  // namespace

Annotations::Annotations(Library* library) : m_connection(library ? library->connectionName() : QString()) {}

bool Annotations::isOpen() const { return !m_connection.isEmpty() && QSqlDatabase::database(m_connection, false).isOpen(); }

QColor Annotations::colorValue(const QString& key)
{
    if (key == QLatin1String("pink")) return QColor(0xE0, 0x6C, 0xA0);
    if (key == QLatin1String("orange")) return QColor(0xEB, 0x96, 0x3E);
    if (key == QLatin1String("blue")) return QColor(0x5A, 0x9B, 0xE6);
    return QColor(0xE3, 0xBF, 0x3A);   // yellow
}

int Annotations::add(const Annotation& a)
{
    if (!isOpen() || a.bookId <= 0 || a.spine < 0) return 0;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT INTO annotations (book_id, kind, spine, sel_start, sel_end, cfi, text, color, note, page_label, chapter, created, modified)"
                             " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    q.addBindValue(a.bookId);
    q.addBindValue(int(a.kind));
    q.addBindValue(a.spine);
    q.addBindValue(a.start);
    q.addBindValue(qMax(a.start, a.end));
    q.addBindValue(text(a.cfi));
    q.addBindValue(text(a.text));
    q.addBindValue(text(a.kind == Annotation::Highlight ? (colors().contains(a.color) ? a.color : QStringLiteral("yellow")) : QString()));
    q.addBindValue(text(a.note));
    q.addBindValue(text(a.pageLabel));
    q.addBindValue(text(a.chapter));
    q.addBindValue(iso(a.created.isValid() ? a.created : now));
    q.addBindValue(iso(now));
    return q.exec() ? q.lastInsertId().toInt() : 0;
}

bool Annotations::update(const Annotation& a)
{
    if (!isOpen() || a.id <= 0) return false;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("UPDATE annotations SET color=?, note=?, modified=? WHERE id=?"));
    q.addBindValue(text(a.kind == Annotation::Highlight ? (colors().contains(a.color) ? a.color : QStringLiteral("yellow")) : QString()));
    q.addBindValue(text(a.note));
    q.addBindValue(iso(QDateTime::currentDateTimeUtc()));
    q.addBindValue(a.id);
    return q.exec() && q.numRowsAffected() == 1;
}

void Annotations::remove(int id)
{
    if (!isOpen()) return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("DELETE FROM annotations WHERE id=?"));
    q.addBindValue(id);
    q.exec();
}

Annotation Annotations::get(int id) const
{
    if (!isOpen()) return {};
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QString::fromLatin1(kColumns) + QStringLiteral("WHERE id=?"));
    q.addBindValue(id);
    return q.exec() && q.next() ? read(q) : Annotation();
}

QList<Annotation> Annotations::forBook(int bookId) const
{
    QList<Annotation> out;
    if (!isOpen()) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QString::fromLatin1(kColumns) + QStringLiteral("WHERE book_id=? ORDER BY spine, sel_start, id"));
    q.addBindValue(bookId);
    if (q.exec())
        while (q.next()) out << read(q);
    return out;
}

QList<Annotation> Annotations::inChapter(int bookId, int spine) const
{
    QList<Annotation> out;
    if (!isOpen()) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QString::fromLatin1(kColumns) + QStringLiteral("WHERE book_id=? AND spine=? ORDER BY sel_start, id"));
    q.addBindValue(bookId);
    q.addBindValue(spine);
    if (q.exec())
        while (q.next()) out << read(q);
    return out;
}

QList<Annotation> Annotations::highlightsAt(int bookId, int spine, int start, int end) const
{
    QList<Annotation> out;
    for (const Annotation& a : inChapter(bookId, spine))
        if (a.kind == Annotation::Highlight && a.start < qMax(end, start + 1) && a.end > start) out << a;
    return out;
}

int Annotations::bookmarkAt(int bookId, int spine, int from, int to) const
{
    for (const Annotation& a : inChapter(bookId, spine))
        if (a.kind == Annotation::Bookmark && a.start >= from && a.start <= to) return a.id;
    return 0;
}

int Annotations::count(int bookId) const
{
    if (!isOpen()) return 0;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM annotations WHERE book_id=?"));
    q.addBindValue(bookId);
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

QString Annotations::markdown(int bookId, const QString& title, const QString& author) const
{
    QString md = QStringLiteral("# %1\n").arg(title.isEmpty() ? QStringLiteral("Notebook") : title);
    if (!author.isEmpty()) md += QStringLiteral("\n%1\n").arg(author);
    QString chapter;
    QStringList bookmarks;
    for (const Annotation& a : forBook(bookId)) {
        if (a.kind == Annotation::Bookmark) {
            bookmarks << QStringLiteral("- Page %1%2").arg(a.pageLabel, a.chapter.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(a.chapter));
            continue;
        }
        if (a.chapter != chapter && !a.chapter.isEmpty()) {
            chapter = a.chapter;
            md += QStringLiteral("\n## %1\n").arg(chapter);
        }
        QString quote = a.text.simplified();
        md += QStringLiteral("\n> %1\n").arg(quote);
        md += QStringLiteral("\n*Page %1 · %2*\n").arg(a.pageLabel, a.color);
        if (!a.note.trimmed().isEmpty()) {
            md += QLatin1Char('\n');
            for (const QString& line : a.note.trimmed().split(QLatin1Char('\n'))) md += QStringLiteral("%1\n").arg(line);
        }
    }
    if (!bookmarks.isEmpty()) md += QStringLiteral("\n## Bookmarks\n\n") + bookmarks.join(QLatin1Char('\n')) + QLatin1Char('\n');
    return md;
}

}  // namespace core

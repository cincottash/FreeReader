#include "KindleImportDialog.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

KindleImportDialog::KindleImportDialog(const core::KindleExport& ex, const core::KindlePlan& plan, const QString& bookTitle, Apply apply,
                                       QWidget* parent)
    : QDialog(parent), m_apply(std::move(apply))
{
    // A Biblioglot Notebook export or a Kindle's: the same choices, worded for where they come from.
    const bool nb = plan.notebook;
    setWindowTitle(nb ? tr("Import Highlights") : tr("Import Kindle Highlights"));
    const QLocale loc;
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 14, 18, 14);
    lay->setSpacing(10);

    int words = 0, missingWords = 0, missingPassages = 0;
    for (const core::KindlePlaced& p : plan.items) {
        if (p.item.bookmark) continue;
        const int n = int(p.item.text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts).size());
        if (!p.placed() && (n > 4 || !p.item.note.isEmpty())) ++missingPassages;
        if (n > 4) continue;
        (p.found ? words : missingWords) += 1;
    }
    QString s = tr("From <b>%1</b> into <b>%2</b>:").arg(ex.title.toHtmlEscaped(), bookTitle.toHtmlEscaped());
    s += QStringLiteral("<ul style='margin-left:-20px'>");
    s += QStringLiteral("<li>") + tr("%1 highlights found in this book").arg(loc.toString(plan.found));
    if (plan.notes) s += tr(" (%1 with notes)").arg(loc.toString(plan.notes));
    s += QStringLiteral("</li>");
    if (plan.notFound)
        s += QStringLiteral("<li>") +
             (nb ? tr("%1 not in this book's text: the file's edition words them differently")
                 : tr("%1 not in this book's text: your Kindle edition words them differently")).arg(loc.toString(plan.notFound)) +
             QStringLiteral("</li>");
    const int equivalents = plan.byForm + plan.byPosition;
    if (plan.bookmarks) s += QStringLiteral("<li>") + tr("%1 bookmarks").arg(loc.toString(plan.bookmarks)) + QStringLiteral("</li>");
    if (plan.lastLocation)
        s += QStringLiteral("<li>") + tr("Your Kindle place: location %1, about %2 % through the book")
                                         .arg(loc.toString(plan.lastLocation), loc.toString(qRound(plan.lastFraction * 100))) + QStringLiteral("</li>");
    s += QStringLiteral("</ul>");
    m_summary = new QLabel(s);
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setProperty("testId", "kindleSummary");
    lay->addWidget(m_summary);

    // What didn't match exactly, and what that affects: said before anything is imported.
    int notesAffected = 0;
    for (const core::KindlePlaced& p : plan.items)
        if (!p.found && !p.item.bookmark && !p.item.note.isEmpty()) ++notesAffected;
    m_warning = new QLabel;
    m_warning->setWordWrap(true);
    m_warning->setProperty("testId", "kindleWarning");
    m_warning->setObjectName(QStringLiteral("Warning"));
    m_warning->setStyleSheet(QStringLiteral("QLabel#Warning { border: 1px solid #c98a2e; border-radius: 6px; padding: 8px 10px; }"));
    if (plan.notFound) {
        QString w = plan.notFound == 1 ? tr("⚠ 1 highlight didn't match this book's text exactly")
                                       : tr("⚠ %1 highlights didn't match this book's text exactly").arg(loc.toString(plan.notFound));
        if (notesAffected) w += notesAffected == 1 ? tr(" (1 with a note)") : tr(" (%1 with notes)").arg(loc.toString(notesAffected));
        w += QStringLiteral(". ");
        w += equivalents
                 ? (nb ? tr("They'll go on the closest equivalent in orange, with the file's words as a note: %1 the same word in another form, %2 at the matching spot. Check the list below before importing.")
                       : tr("They'll go on the closest equivalent in orange, with the Kindle words as a note: %1 the same word in another form, %2 at the matching spot. Check the list below before importing."))
                       .arg(loc.toString(plan.byForm), loc.toString(plan.byPosition))
                 : tr("Check the list below before importing.");
        m_warning->setText(w);
    }
    m_warning->setVisible(plan.notFound > 0);
    lay->addWidget(m_warning);

    auto check = [&](const QString& text, const char* id, bool on, bool enabled) {
        auto* c = new QCheckBox(text);
        c->setChecked(on && enabled);
        c->setEnabled(enabled);
        c->setProperty("testId", id);
        lay->addWidget(c);
        return c;
    };
    m_highlights = check(tr("Highlight Them in the Book (With Their Notes)"), "kindleHighlights", true, plan.found > 0);
    m_equivalents = check((nb ? tr("Highlight the Equivalent Words for the Others, in Orange, With the File's Words as a Note (%1)")
                              : tr("Highlight the Equivalent Words for the Others, in Orange, With the Kindle Words as a Note (%1)"))
                              .arg(loc.toString(equivalents)),
                          "kindleEquivalents", true, equivalents > 0);
    m_words = check(tr("Add the Words and Short Phrases to the Word List (%1)").arg(loc.toString(words)), "kindleWords", true, words > 0);
    m_missing = check((nb ? tr("Also Add the Words This Edition Says Differently (%1)")
                          : tr("Also Add the Kindle Words This Edition Says Differently (%1), Marked as From Kindle"))
                          .arg(loc.toString(missingWords)),
                      "kindleMissing", true, missingWords > 0);
    m_passages = check(tr("Keep the Passages and Notes This Edition Doesn't Have (%1) as Bookmarks at About Their Place").arg(loc.toString(missingPassages)),
                       "kindlePassages", true, missingPassages > 0);
    m_passages->setVisible(missingPassages > 0);
    m_bookmarks = check(tr("Add the Bookmarks (%1)").arg(loc.toString(plan.bookmarks)), "kindleBookmarks", true, plan.bookmarks > 0);
    m_move = check(tr("Continue Reading From My Kindle Place (About %1 %)").arg(loc.toString(qRound(plan.lastFraction * 100))), "kindleMove",
                   true, plan.lastLocation > 0);
    m_move->setVisible(!nb);   // a Notebook export has no reading place
    m_missing->setToolTip(nb ? tr("They go to the Word List as the file has them, with this edition's sentence around their equivalent.")
                             : tr("They go to the Word List as highlighted on the Kindle, with this edition's sentence around their equivalent."));

    m_showMissing = new QPushButton((nb ? tr("Show the File's Words and Their Equivalents (%1)") : tr("Show the Kindle Words and Their Equivalents (%1)"))
                                        .arg(loc.toString(plan.notFound)));
    m_showMissing->setObjectName(QStringLiteral("Link"));
    m_showMissing->setCursor(Qt::PointingHandCursor);
    m_showMissing->setVisible(plan.notFound > 0);
    m_showMissing->setProperty("testId", "kindleShowMissing");
    lay->addWidget(m_showMissing, 0, Qt::AlignLeft);
    m_missingList = new QListWidget;
    m_missingList->setProperty("testId", "kindleMissingList");
    for (const core::KindlePlaced& p : plan.items)
        if (!p.found && !p.item.bookmark) {
            const QString how = p.match == core::KindlePlaced::ByForm       ? tr("another form")
                              : p.match == core::KindlePlaced::ByPosition ? tr("matching spot")
                                                                          : QString();
            if (nb)
                m_missingList->addItem(how.isEmpty() ? tr("%1 (no equivalent)").arg(p.item.text) : tr("%1 → %2 (%3)").arg(p.item.text, p.equivalent, how));
            else
                m_missingList->addItem(how.isEmpty() ? tr("Location %1: %2 (no equivalent)").arg(p.item.location).arg(p.item.text)
                                                     : tr("Location %1: %2 → %3 (%4)").arg(p.item.location).arg(p.item.text, p.equivalent, how));
        }
    m_missingList->setMinimumHeight(180);
    m_missingList->hide();
    lay->addWidget(m_missingList, 1);
    connect(m_showMissing, &QPushButton::clicked, this, [this] {
        m_missingList->setVisible(!m_missingList->isVisible());
        adjustSize();
    });

    m_result = new QLabel;
    m_result->setWordWrap(true);
    m_result->setProperty("testId", "kindleResult");
    m_result->hide();
    lay->addWidget(m_result);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_close = new QPushButton(tr("Cancel"));
    m_import = new QPushButton(tr("Import"));
    m_import->setDefault(true);
    m_import->setObjectName(QStringLiteral("Suggested"));
    m_import->setProperty("testId", "kindleImport");
    m_close->setProperty("testId", "kindleClose");
    buttons->addWidget(m_close);
    buttons->addWidget(m_import);
    lay->addLayout(buttons);
    connect(m_import, &QPushButton::clicked, this, &KindleImportDialog::runImport);
    connect(m_close, &QPushButton::clicked, this, [this] { m_done ? accept() : reject(); });
    resize(560, sizeHint().height());
}

ReaderView::KindleOptions KindleImportDialog::options() const
{
    ReaderView::KindleOptions o;
    o.highlights = m_highlights->isChecked();
    o.words = m_words->isChecked();
    o.missingWords = m_missing->isChecked();
    o.missingPassages = m_passages->isChecked();
    o.equivalents = m_equivalents->isChecked();
    o.bookmarks = m_bookmarks->isChecked();
    o.moveToLastRead = m_move->isChecked();
    return o;
}

QString KindleImportDialog::summary() const { return m_summary->text(); }
QString KindleImportDialog::warning() const { return m_warning->isVisibleTo(this) ? m_warning->text() : QString(); }
QString KindleImportDialog::resultText() const { return m_result->text(); }

void KindleImportDialog::runImport()
{
    if (m_done || !m_apply) return;
    const ReaderView::KindleResult r = m_apply(options());
    m_done = true;
    const QLocale loc;
    QStringList lines;
    lines << tr("Imported: %1 highlights, %2 equivalent highlights (orange), %3 words to the Word List, %4 words this edition says differently, %5 bookmarks.")
                 .arg(loc.toString(r.highlights), loc.toString(r.equivalents), loc.toString(r.words), loc.toString(r.missingWords), loc.toString(r.bookmarks));
    if (r.passages) lines << tr("%1 passages and notes this edition doesn't have are bookmarks at about their place.").arg(loc.toString(r.passages));
    if (r.skipped) lines << tr("%1 were already there and were left as they are.").arg(loc.toString(r.skipped));
    if (r.moved) lines << tr("The book is open at your Kindle place.");
    m_result->setText(lines.join(QLatin1Char('\n')));
    m_result->show();
    m_import->setEnabled(false);
    for (QCheckBox* c : {m_highlights, m_equivalents, m_words, m_missing, m_passages, m_bookmarks, m_move}) c->setEnabled(false);
    m_close->setText(tr("Close"));
}

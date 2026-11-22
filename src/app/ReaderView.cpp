#include "ReaderView.h"

#include "FooterBar.h"
#include "Icons.h"
#include "Popups.h"
#include "Settings.h"
#include "TocPanel.h"

#include "core/services/BookEngine.h"
#include "core/services/Library.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace {

QToolButton* iconButton(const char* icon, const QString& tip, const Theme& t)
{
    auto* b = new QToolButton;
    b->setObjectName(QStringLiteral("Icon"));
    b->setIcon(icons::icon(icon, t.fg));
    b->setIconSize(QSize(20, 20));
    b->setFixedSize(40, 40);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

QString minutesText(qint64 chars, int speed)
{
    const double minutes = double(chars) / double(qMax(1, speed));
    if (minutes < 1) return QObject::tr("Less than 1 min left in chapter");
    const int m = int(std::lround(minutes));
    if (m < 60) return QObject::tr("%1 min left in chapter").arg(QLocale().toString(m));
    return QObject::tr("%1 h %2 min left in chapter").arg(QLocale().toString(m / 60)).arg(m % 60);
}

}  // namespace

ReaderView::ReaderView(core::BookEngine* engine, core::Library* library, Settings& settings, QWidget* parent)
    : QWidget(parent), m_engine(engine), m_library(library), m_settings(settings)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // Header
    auto* bar = new QWidget;
    bar->setObjectName(QStringLiteral("Bar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 4, 8, 4);
    h->setSpacing(6);
    m_libraryButton = iconButton("back", tr("Back to the start screen"), m_theme);
    h->addWidget(m_libraryButton);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(1);
    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("Title"));
    m_subtitle = new QLabel;
    m_subtitle->setObjectName(QStringLiteral("Muted"));
    m_title->setProperty("testId", "title");
    m_subtitle->setProperty("testId", "subtitle");
    m_subtitle->setStyleSheet(QStringLiteral("font-size: 12px;"));
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    h->addLayout(titles, 1);
    m_notKept = new QPushButton(tr("Settings Not Kept"));
    m_notKept->setObjectName(QStringLiteral("Link"));
    m_notKept->setCursor(Qt::PointingHandCursor);
    m_notKept->setToolTip(tr("App data is kept in a temporary folder and deleted when the app closes. Click to choose a folder."));
    m_notKept->hide();
    h->addWidget(m_notKept);
    m_tocButton = iconButton("toc", tr("Contents (Ctrl+T)"), m_theme);
    m_tocButton->setCheckable(true);
    m_tocButton->setProperty("testId", "tocButton");
    m_commandsButton = iconButton("commands", tr("Commands (Ctrl+K)"), m_theme);
    m_fullButton = iconButton("fullscreen", tr("Full screen (F11)"), m_theme);
    h->addWidget(m_tocButton);
    h->addWidget(m_commandsButton);
    h->addWidget(m_fullButton);
    lay->addWidget(bar);

    // Body: contents panel + pages
    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    m_toc = new TocPanel;
    m_toc->setFixedWidth(320);
    m_toc->hide();
    body->addWidget(m_toc);
    m_pages = new PageView(engine);
    body->addWidget(m_pages, 1);
    lay->addLayout(body, 1);

    m_footer = new FooterBar;
    lay->addWidget(m_footer);

    m_backChip = new QPushButton(m_pages);
    m_backChip->setObjectName(QStringLiteral("Chip"));
    m_backChip->setCursor(Qt::PointingHandCursor);
    m_backChip->setFocusPolicy(Qt::NoFocus);
    m_backChip->hide();
    m_backChip->setProperty("testId", "backChip");
    m_note = new NotePopup(m_pages);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);
    connect(&m_saveTimer, &QTimer::timeout, this, &ReaderView::savePosition);

    connect(m_libraryButton, &QToolButton::clicked, this, &ReaderView::libraryRequested);
    connect(m_commandsButton, &QToolButton::clicked, this, &ReaderView::commandsRequested);
    connect(m_fullButton, &QToolButton::clicked, this, &ReaderView::fullScreenRequested);
    connect(m_notKept, &QPushButton::clicked, this, &ReaderView::settingsLinkClicked);
    connect(m_tocButton, &QToolButton::toggled, this, [this](bool on) { setContentsVisible(on); });
    connect(m_toc, &TocPanel::entryActivated, this, [this](int i) {
        const auto ix = m_engine->index();
        if (!ix || i < 0 || size_t(i) >= ix->toc.size() || ix->toc[size_t(i)].spine < 0) return;
        jumpTo({ix->toc[size_t(i)].spine, ix->toc[size_t(i)].offset});
        m_pages->setFocus();
    });
    connect(m_backChip, &QPushButton::clicked, this, &ReaderView::goBack);
    connect(m_pages, &PageView::positionChanged, this, &ReaderView::onPositionChanged);
    connect(m_pages, &PageView::readyChanged, this, &ReaderView::updateHeaderAndFooter);
    connect(m_pages, &PageView::linkClicked, this, &ReaderView::onLink);
    connect(m_pages, &PageView::imageClicked, this, &ReaderView::imageRequested);
    connect(m_pages, &PageView::pageTurnedByUser, m_note, &QWidget::hide);
    connect(m_engine, &core::BookEngine::indexReady, this, &ReaderView::onIndexReady);
    connect(m_footer, &FooterBar::jumpRequested, this, [this](double f) {
        const auto ix = m_engine->index();
        if (ix) jumpTo(ix->fromGlobal(qint64(f * double(ix->total))));
    });
}

void ReaderView::setSettingsNotKept(bool on) { m_notKept->setVisible(on); }

void ReaderView::applyReadingOptions()
{
    m_pages->setReadingOptions(m_settings.textSizePct, m_settings.lineSpacingPct, m_settings.columns);
}

void ReaderView::showBook(int bookId, const core::Position& start)
{
    m_bookId = bookId;
    m_history.clear();
    m_backChip->hide();
    m_note->hide();
    const core::Book* book = m_engine->book();
    m_title->setText(book ? book->metadata.title : QString());
    m_toc->setEntries({}, {});
    m_footer->setProgress(0, {});
    m_footer->setMarks({});
    setContentsVisible(m_settings.showContents);
    applyReadingOptions();
    m_lastShown = start;
    m_sinceTurn.start();
    m_pages->setPosition(start);
    if (m_engine->index()) onIndexReady();
    updateHeaderAndFooter();
    m_pages->setFocus();
}

void ReaderView::closeBook()
{
    if (m_bookId) savePosition();
    m_saveTimer.stop();
    m_bookId = 0;
    m_note->hide();
}

bool ReaderView::contentsVisible() const { return m_toc->isVisible(); }

void ReaderView::setContentsVisible(bool on)
{
    m_toc->setVisible(on);
    QSignalBlocker block(m_tocButton);
    m_tocButton->setChecked(on);
    m_settings.showContents = on;
}

bool ReaderView::isSettled() const { return m_engine->isOpen() && m_engine->index() && m_pages->isReady(); }

void ReaderView::setFullScreenIcon(bool fullScreen)
{
    m_fullButton->setIcon(icons::icon(fullScreen ? "exitFullscreen" : "fullscreen", m_theme.fg));
    m_fullButton->setToolTip(fullScreen ? tr("Leave full screen (F11)") : tr("Full screen (F11)"));
}

void ReaderView::onIndexReady()
{
    const auto ix = m_engine->index();
    const core::Book* book = m_engine->book();
    if (!ix || !book) return;
    std::vector<bool> reachable;
    for (const auto& t : ix->toc) reachable.push_back(t.spine >= 0);
    m_toc->setEntries(book->toc, reachable);
    // Chapter marks: the top level, or two levels when that isn't too many.
    std::vector<double> marks;
    int depthLimit = 1;
    int count = 0;
    for (const auto& e : book->toc) count += e.depth <= 1;
    if (count > 80) depthLimit = 0;
    for (size_t i = 0; i < book->toc.size(); ++i)
        if (book->toc[i].depth <= depthLimit && ix->toc[i].spine >= 0 && ix->total > 0 && ix->toc[i].global > 0)
            marks.push_back(double(ix->toc[i].global) / double(ix->total));
    m_footer->setMarks(marks);
    updateHeaderAndFooter();
}

QString ReaderView::chapterTitleAt(const core::Position& p) const
{
    const auto ix = m_engine->index();
    const core::Book* book = m_engine->book();
    if (!ix || !book) return {};
    const int e = ix->tocEntryAt(p);
    return e >= 0 ? book->toc[size_t(e)].title : QString();
}

void ReaderView::updateHeaderAndFooter()
{
    const core::Book* book = m_engine->book();
    if (!book) return;
    const core::Position pos = m_pages->position();
    QStringList sub;
    const QString chapter = chapterTitleAt(pos);
    if (!chapter.isEmpty()) sub << chapter;
    if (!book->metadata.language.isEmpty()) {
        const QLocale loc(book->metadata.language);
        if (loc.language() != QLocale::C) sub << QLocale::languageToString(loc.language());
    }
    m_subtitle->setText(sub.join(QStringLiteral(" · ")));
    m_subtitle->setVisible(!sub.isEmpty());

    const auto ix = m_engine->index();
    if (!ix) return;
    m_toc->setCurrent(ix->tocEntryAt(pos));
    const double fraction = ix->fraction(pos);
    QStringList stats;
    stats << tr("Page %1 of %2").arg(ix->pageLabel(pos), ix->lastPageLabel());
    stats << QLocale().toString(int(std::floor(fraction * 100))) + QStringLiteral("%");
    // Time left in the chapter, from the reading speed learned from page turns.
    const qint64 g = ix->global(pos);
    qint64 next = ix->total;
    for (const auto& t : ix->toc)
        if (t.spine >= 0 && t.global > g && t.global < next) next = t.global;
    stats << minutesText(next - g, m_settings.readingSpeed);
    m_footer->setProgress(fraction, stats);
}

void ReaderView::onPositionChanged(const core::Position& p, bool byTurn)
{
    // Learn the reading speed from forward page turns at a plausible pace.
    if (byTurn && m_lastShown < p && m_lastScreenChars > 200) {
        const double seconds = m_sinceTurn.elapsed() / 1000.0;
        if (seconds > 3 && seconds < 600) {
            const double cpm = m_lastScreenChars / (seconds / 60.0);
            m_settings.readingSpeed = qBound(200, int(0.85 * m_settings.readingSpeed + 0.15 * cpm), 6000);
        }
    }
    m_sinceTurn.restart();
    m_lastShown = p;
    const core::Position end = m_pages->screenEnd();
    m_lastScreenChars = end.spine == p.spine ? qMax(0, end.offset - p.offset) : 0;
    updateHeaderAndFooter();
    if (m_bookId) m_saveTimer.start();
}

void ReaderView::savePosition()
{
    if (!m_bookId || !m_library) return;
    core::SavedPosition s;
    s.position = m_pages->position();
    s.cfi = m_engine->cfiFor(s.position);
    const auto ix = m_engine->index();
    s.progress = ix ? ix->fraction(s.position) : 0;
    m_library->savePosition(m_bookId, s);
}

void ReaderView::jumpTo(const core::Position& p)
{
    const core::Position here = m_pages->position();
    if (here != p) {
        m_history.push_back(here);
        if (m_history.size() > 50) m_history.erase(m_history.begin());
    }
    m_note->hide();
    m_pages->setPosition(p);
    placeBackChip();
}

void ReaderView::goBack()
{
    if (m_history.empty()) return;
    const core::Position p = m_history.back();
    m_history.pop_back();
    m_pages->setPosition(p);
    placeBackChip();
    m_pages->setFocus();
}

void ReaderView::placeBackChip()
{
    const auto ix = m_engine->index();
    if (m_history.empty()) {
        m_backChip->hide();
        return;
    }
    const core::Position& p = m_history.back();
    m_backChip->setText(ix ? tr("Back to Page %1").arg(ix->pageLabel(p)) : tr("Back"));
    m_backChip->setToolTip(tr("Return to where you were before the jump (Alt+Left)."));
    m_backChip->adjustSize();
    m_backChip->move((m_pages->width() - m_backChip->width()) / 2, m_pages->height() - m_backChip->height() - 6);
    m_backChip->show();
    m_backChip->raise();
}

void ReaderView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_backChip->isVisible()) placeBackChip();
}

void ReaderView::nextChapter()
{
    const auto ix = m_engine->index();
    const core::Position pos = m_pages->position();
    if (!ix) {
        m_pages->setPosition({pos.spine + 1, 0});
        return;
    }
    const qint64 g = ix->global(pos);
    qint64 best = -1;
    core::Position target;
    for (const auto& t : ix->toc)
        if (t.spine >= 0 && t.global > g && (best < 0 || t.global < best)) {
            best = t.global;
            target = {t.spine, t.offset};
        }
    if (best >= 0) jumpTo(target);
}

void ReaderView::previousChapter()
{
    const auto ix = m_engine->index();
    const core::Position pos = m_pages->position();
    if (!ix) {
        m_pages->setPosition({qMax(0, pos.spine - 1), 0});
        return;
    }
    // The start of this chapter, or of the one before when already at the start.
    const qint64 g = ix->global(pos);
    qint64 best = -1;
    core::Position target;
    for (const auto& t : ix->toc)
        if (t.spine >= 0 && t.global < g - 1 && t.global > best) {
            best = t.global;
            target = {t.spine, t.offset};
        }
    if (best >= 0) jumpTo(target);
    else jumpTo({0, 0});
}

QString ReaderView::goToPageOrPercent(const QString& raw)
{
    const auto ix = m_engine->index();
    const QString text = raw.trimmed();
    if (!ix) return tr("The book is still being prepared. Try again in a moment.");
    const QString usage = tr("Type a page number from 1 to %1, or a percentage such as 45%.").arg(ix->lastPageLabel());
    if (text.isEmpty()) return usage;
    static const QRegularExpression pct(QStringLiteral("^(\\d+(?:[.,]\\d+)?)\\s*%$"));
    const auto m = pct.match(text);
    if (m.hasMatch()) {
        const double v = QString(m.captured(1)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        if (v < 0 || v > 100) return usage;
        jumpTo(ix->fromGlobal(qint64(v / 100.0 * double(ix->total))));
        return {};
    }
    // Printed page labels ("xii") when the book has them.
    if (!ix->printedPages.empty()) {
        for (size_t i = 0; i < ix->printedPages.size(); ++i)
            if (ix->printedPages[i].label.compare(text, Qt::CaseInsensitive) == 0) {
                jumpTo(ix->fromGlobal(ix->printedPages[i].global));
                return {};
            }
    }
    bool ok = false;
    const int page = QLocale().toInt(text, &ok);
    const int page2 = ok ? page : text.toInt(&ok);
    if (!ok || page2 < 1 || page2 > ix->pageCount()) return usage;
    jumpTo(ix->positionOfPage(page2));
    return {};
}

void ReaderView::onLink(const LinkClick& link, const QPoint& globalPos)
{
    if (link.external) {
        if (m_interactive) QDesktopServices::openUrl(QUrl(link.target));
        return;
    }
    const auto ix = m_engine->index();
    const core::Book* book = m_engine->book();
    if (!ix || !book) return;
    core::Position target;
    if (!ix->positionFor(*book, link.target, &target)) return;
    const core::NoteText note = link.noteRef || link.target.contains(QLatin1Char('#')) ? m_engine->noteText(link.target) : core::NoteText{};
    if ((link.noteRef || note.isNote) && !note.text.isEmpty()) {
        const QPoint local = m_pages->mapFromGlobal(globalPos);
        m_note->showNote(note.text, QRect(local - QPoint(4, 10), QSize(8, 20)), [this, target] { jumpTo(target); });
        return;
    }
    jumpTo(target);
}

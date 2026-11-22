#include "ReaderView.h"

#include "AppearancePanel.h"
#include "FooterBar.h"
#include "Icons.h"
#include "Popups.h"
#include "Settings.h"
#include "TocPanel.h"
#include "NotebookPanel.h"
#include "SearchPanel.h"
#include "TranslationPopup.h"

#include "core/services/BookEngine.h"
#include "core/services/Library.h"
#include "core/translate/TranslationService.h"
#include "core/services/BookIndex.h"
#include "core/services/WordLog.h"
#include "core/services/Annotations.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QWidgetAction>

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonObject>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QLocale>
#include <QMessageBox>
#include <QStandardPaths>
#include <QMenu>
#include <QUrlQuery>
#ifdef HAVE_TEXT_TO_SPEECH
#include <QTextToSpeech>
#endif
#include <QPainter>
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
    m_libraryButton = iconButton("back", tr("Back to the library (Ctrl+L)"), m_theme);
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
    m_fullButton = iconButton("fullscreen", tr("Full screen (F11)"), m_theme);
    m_searchButton = iconButton("search", tr("Search This Book (Ctrl+F)"), m_theme);
    m_searchButton->setCheckable(true);
    m_searchButton->setProperty("testId", "searchButton");
    m_bookmarkButton = iconButton("bookmark", tr("Bookmark This Page (Ctrl+D)"), m_theme);
    m_bookmarkButton->setCheckable(true);
    m_bookmarkButton->setProperty("testId", "bookmarkButton");
    m_notebookButton = iconButton("panel", tr("Notebook: highlights, notes, bookmarks (Ctrl+Shift+N)"), m_theme);
    m_notebookButton->setCheckable(true);
    m_notebookButton->setProperty("testId", "notebookButton");
    h->addWidget(m_searchButton);
    h->addWidget(m_tocButton);
    m_aaButton = new QToolButton;
    m_aaButton->setObjectName(QStringLiteral("Icon"));
    m_aaButton->setText(QStringLiteral("Aa"));
    QFont aa(QStringLiteral("Literata"));
    aa.setPixelSize(17);
    aa.setWeight(QFont::DemiBold);
    m_aaButton->setFont(aa);
    m_aaButton->setFixedSize(40, 40);
    m_aaButton->setCursor(Qt::PointingHandCursor);
    m_aaButton->setFocusPolicy(Qt::NoFocus);
    m_aaButton->setToolTip(tr("Text and layout: font, size, spacing, margins, columns"));
    m_aaButton->setProperty("testId", "aaButton");
    h->addWidget(m_aaButton);    // Recently used themes as dots (as in the mock-up), then the full theme list.
    m_themeDots = new QWidget;
    m_themeDots->setLayout(new QHBoxLayout);
    m_themeDots->layout()->setContentsMargins(6, 0, 6, 0);
    m_themeDots->layout()->setSpacing(0);
    h->addWidget(m_themeDots);
    h->addWidget(m_bookmarkButton);
    h->addWidget(m_notebookButton);
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
    m_search = new SearchPanel;
    m_search->setFixedWidth(320);
    m_search->hide();
    body->addWidget(m_search);
    m_pages = new PageView(engine);
    body->addWidget(m_pages, 1);
    m_notebook = new NotebookPanel;
    m_notebook->setFixedWidth(340);
    m_notebook->hide();
    body->addWidget(m_notebook);
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
    m_translate = new TranslationPopup(m_pages);
    connect(m_translate, &TranslationPopup::settingsRequested, this, [this] {
        m_translate->hide();
        emit translationSettingsRequested();
    });
    connect(m_translate, &TranslationPopup::speakRequested, this, &ReaderView::speak);
    m_panel = new AppearancePanel(this);
    m_lookTimer.setSingleShot(true);
    m_lookTimer.setInterval(60);   // sliders move in many small steps; lay out once they pause
    connect(&m_lookTimer, &QTimer::timeout, this, &ReaderView::applyReadingOptions);
    connect(m_aaButton, &QToolButton::clicked, this, [this] {
        if (m_panel->isVisible()) {
            m_panel->hide();
            return;
        }
        applyReadingOptions();
        m_panel->showBelow(m_aaButton->mapTo(this, QPoint(m_aaButton->width() + 140, m_aaButton->height() + 4)));
    });
    connect(m_panel, &AppearancePanel::valueChanged, this, &ReaderView::changeAppearance);
    connect(m_panel, &AppearancePanel::themesRequested, this, [this] {
        m_panel->hide();
        emit moreThemesRequested(m_aaButton->mapTo(window(), QPoint(m_aaButton->width() + 140, m_aaButton->height())));
    });
    connect(m_panel, &AppearancePanel::resetRequested, this, [this](int scope) {
        if (!m_library) return;
        if (scope == AppearancePanel::ThisBook && m_bookId) m_library->setAppearanceLayer(QStringLiteral("book:%1").arg(m_bookId), {});
        if (scope == AppearancePanel::Language && !languageCode().isEmpty())
            m_library->setAppearanceLayer(QStringLiteral("lang:") + languageCode(), {});
        applyReadingOptions();
    });
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);
    connect(&m_saveTimer, &QTimer::timeout, this, &ReaderView::savePosition);

    connect(m_libraryButton, &QToolButton::clicked, this, &ReaderView::libraryRequested);
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
    connect(m_pages, &PageView::pageTurnedByUser, m_translate, &QWidget::hide);
    connect(m_searchButton, &QToolButton::toggled, this, [this](bool on) { on ? openSearch() : closeSearch(); });
    connect(m_bookmarkButton, &QToolButton::clicked, this, [this] {
        toggleBookmark();
        m_pages->setFocus();
    });
    connect(m_notebookButton, &QToolButton::toggled, this, [this](bool on) { setNotebookVisible(on); });
    connect(m_notebook, &NotebookPanel::activated, this, &ReaderView::goToAnnotation);
    connect(m_notebook, &NotebookPanel::editNoteRequested, this, &ReaderView::editNote);
    connect(m_notebook, &NotebookPanel::colorRequested, this, [this](int id, const QString& color) {
        if (!m_notes) return;
        core::Annotation a = m_notes->get(id);
        a.color = color;
        m_notes->update(a);
        refreshAnnotations();
    });
    connect(m_notebook, &NotebookPanel::deleteRequested, this, [this](int id) {
        if (!m_notes) return;
        m_notes->remove(id);
        refreshAnnotations();
    });
    connect(m_notebook, &NotebookPanel::exportRequested, this, [this] {
        if (!m_interactive || !m_bookId) return;
        const QString name = m_title->text().isEmpty() ? tr("Notebook") : m_title->text();
        const QString path = QFileDialog::getSaveFileName(this, tr("Export Notebook"),
                                                          QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') + name + QStringLiteral(" notes.md"),
                                                          tr("Markdown (*.md);;JSON (*.json)"));
        if (path.isEmpty()) return;
        QString err;
        if (!exportNotebook(path, &err)) QMessageBox::warning(this, tr("Export Notebook"), tr("Couldn't save the file. %1").arg(err));
    });
    connect(m_search, &SearchPanel::searchRequested, this, [this](const QString& q, const core::SearchOptions& o) {
        m_search->setSearching();
        m_searchId = m_engine->search(q, o);
    });
    connect(m_engine, &core::BookEngine::searchFinished, this, [this](int id, const QList<core::SearchHit>& hits, bool truncated) {
        if (id != m_searchId) return;
        QStringList labels;
        const auto ix = m_engine->index();
        for (const core::SearchHit& h : hits) labels << (ix ? ix->pageLabel({h.spine, h.start}) : QString());
        m_search->setResults(hits, labels, truncated);
    });
    connect(m_search, &SearchPanel::hitActivated, this, &ReaderView::goToSearchHit);
    connect(m_search, &SearchPanel::closeRequested, this, [this] {
        closeSearch();
        m_pages->setFocus();
    });
    connect(m_pages, &PageView::selectionCleared, m_translate, &QWidget::hide);
    connect(m_pages, &PageView::resized, m_translate, &QWidget::hide);
    connect(m_pages, &PageView::resized, m_note, &QWidget::hide);
    connect(m_pages, &PageView::selectionMade, this, [this] {
        // A slip of the mouse that selects a letter or two, or only punctuation, isn't worth a lookup.
        const QString t = m_pages->selectedText();
        int letters = 0;
        for (const QChar c : t)
            if (c.isLetter()) ++letters;
        if (m_settings.autoTranslate && letters >= 2) translateSelection();
    });
    connect(m_pages, &PageView::selectionMenuRequested, this, [this](const QPoint& at) {
        QMenu* menu = buildSelectionMenu();
        if (m_interactive) menu->exec(at);
        delete menu;
    });
    connect(m_engine, &core::BookEngine::indexReady, this, &ReaderView::onIndexReady);
    connect(m_footer, &FooterBar::jumpRequested, this, [this](double f) {
        const auto ix = m_engine->index();
        if (ix) jumpTo(ix->fromGlobal(qint64(f * double(ix->total))));
    });
}

void ReaderView::setSettingsNotKept(bool on) { m_notKept->setVisible(on); }

void ReaderView::setTheme(const Theme& t)
{
    m_theme = t;
    m_libraryButton->setIcon(icons::icon("back", t.fg));
    m_tocButton->setIcon(icons::icon("toc", t.fg));
    m_searchButton->setIcon(icons::icon("search", t.fg));
    m_bookmarkButton->setIcon(icons::icon("bookmark", t.fg));
    m_notebookButton->setIcon(icons::icon("panel", t.fg));
    m_notebook->setTheme(t);
    setFullScreenIcon(window() && window()->isFullScreen());
    m_pages->setTheme(t);
    m_translate->setTheme(t);
    m_footer->setTheme(t);
    m_toc->setTheme(t);
    updateThemeDots();
}

void ReaderView::updateThemeDots()
{
    QLayout* lay = m_themeDots->layout();
    while (QLayoutItem* it = lay->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    QStringList keys = m_settings.recentThemes;
    if (!keys.contains(m_settings.theme)) keys.prepend(m_settings.theme);
    while (keys.size() > 4) keys.removeLast();
    for (const QString& key : keys) {
        const Theme t = Theme::forChoice(key);
        auto* b = new QToolButton;
        b->setObjectName(QStringLiteral("Icon"));
        b->setFixedSize(32, 40);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(key == QLatin1String("system") ? tr("Match Operating System") : t.name);
        b->setProperty("testId", QStringLiteral("themeDot-") + key);
        QPixmap pm(QSize(22, 22) * 2);
        pm.setDevicePixelRatio(2);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(key == m_settings.theme ? m_theme.accent : m_theme.muted, 2));
        p.setBrush(t.bg);
        p.drawEllipse(QRectF(2, 2, 18, 18));
        p.end();
        b->setIcon(QIcon(pm));
        b->setIconSize(QSize(22, 22));
        connect(b, &QToolButton::clicked, this, [this, key] { emit themeChosen(key); });
        lay->addWidget(b);
    }
    auto* more = new QToolButton;
    more->setObjectName(QStringLiteral("Icon"));
    more->setFixedSize(32, 40);
    more->setCursor(Qt::PointingHandCursor);
    more->setFocusPolicy(Qt::NoFocus);
    more->setIcon(icons::icon("more", m_theme.fg));
    more->setToolTip(tr("All themes (Ctrl+Shift+T)"));
    more->setProperty("testId", "moreThemes");
    connect(more, &QToolButton::clicked, this, [this, more] {
        emit moreThemesRequested(more->mapTo(window(), QPoint(more->width(), more->height())));
    });
    lay->addWidget(more);
}

QString ReaderView::languageCode() const
{
    const core::Book* book = m_engine->book();
    if (!book || book->metadata.language.isEmpty()) return {};
    return book->metadata.language.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
}

QString ReaderView::languageName() const
{
    const QString code = languageCode();
    if (code.isEmpty()) return {};
    const QLocale loc(code);
    return loc.language() == QLocale::C ? QString() : QLocale::languageToString(loc.language());
}

Appearance ReaderView::effectiveAppearance() const
{
    Appearance a = m_settings.look;
    if (m_library) {
        if (!languageCode().isEmpty()) a.merge(m_library->appearanceLayer(QStringLiteral("lang:") + languageCode()));
        if (m_bookId) a.merge(m_library->appearanceLayer(QStringLiteral("book:%1").arg(m_bookId)));
    }
    return a;
}

void ReaderView::applyReadingOptions()
{
    const Appearance a = effectiveAppearance();
    m_pages->setAppearance(a);
    const bool langOwn = m_library && !languageCode().isEmpty() && !m_library->appearanceLayer(QStringLiteral("lang:") + languageCode()).isEmpty();
    const bool bookOwn = m_library && m_bookId && !m_library->appearanceLayer(QStringLiteral("book:%1").arg(m_bookId)).isEmpty();
    m_panel->setValues(a, m_settings.theme == QLatin1String("system") ? tr("Match Operating System") : m_theme.name, languageName(), langOwn, bookOwn);
}

void ReaderView::changeAppearance(const QString& key, const QJsonValue& value)
{
    const QString langScope = languageCode().isEmpty() ? QString() : QStringLiteral("lang:") + languageCode();
    const QString bookScope = m_bookId ? QStringLiteral("book:%1").arg(m_bookId) : QString();
    auto setIn = [&](const QString& scope, bool set) {
        if (scope.isEmpty() || !m_library) return;
        QJsonObject layer = m_library->appearanceLayer(scope);
        if (set) layer[key] = value;
        else layer.remove(key);
        m_library->setAppearanceLayer(scope, layer);
    };
    switch (m_panel->scope()) {
    case AppearancePanel::AllBooks: {
        // Written to All Books, and taken out of this book's and this language's own settings so it shows.
        QJsonObject o = m_settings.look.toJson();
        o[key] = value;
        m_settings.look.merge(o);
        setIn(langScope, false);
        setIn(bookScope, false);
        emit appearanceChanged();
        break;
    }
    case AppearancePanel::Language:
        setIn(langScope, true);
        setIn(bookScope, false);
        break;
    case AppearancePanel::ThisBook:
        setIn(bookScope, true);
        break;
    }
    m_lookTimer.start();
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
    setNotebookVisible(m_settings.showNotebook);
    closeSearch();
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
    m_translate->hide();
    m_lookupId = 0;
    m_engine->cancelSearch();
    m_search->setResults({}, {}, false);
    m_notebook->setAnnotations({});
    m_pages->setHighlights(-1, {});
}

bool ReaderView::contentsVisible() const { return m_toc->isVisible(); }

void ReaderView::setContentsVisible(bool on)
{
    if (on && m_search->isVisible()) closeSearch();
    m_toc->setVisible(on);
    QSignalBlocker block(m_tocButton);
    m_tocButton->setChecked(on);
    m_settings.showContents = on;
}

bool ReaderView::isSettled() const
{
    return m_engine->isOpen() && m_engine->index() && !m_lookTimer.isActive() && m_pages->isReady();
}

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
    stats << QLocale().toString(int(std::floor(fraction * 100 + 1e-6))) + QStringLiteral("%");
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
    refreshAnnotations();
}

void ReaderView::savePosition()
{
    if (!m_bookId || !m_library) return;
    core::SavedPosition s;
    s.position = m_pages->position();
    s.cfi = m_engine->cfiFor(s.position);
    const auto ix = m_engine->index();
    s.progress = ix ? ix->fraction(s.position) : 0;
    if (ix) {
        s.pageLabel = ix->pageLabel(s.position);
        s.pageCount = ix->lastPageLabel();
    }
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

void ReaderView::forgetHistory()
{
    m_history.clear();
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
        // Rounded up to the next character, so "30%" shows as 30 % (not 29.99…).
        jumpTo(ix->fromGlobal(qint64(std::ceil(v / 100.0 * double(ix->total)))));
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

// ---------------------------------------------------------------- translation and the selection menu

void ReaderView::setTranslator(core::TranslationService* service)
{
    m_translator = service;
    if (service) connect(service, &core::TranslationService::finished, this, &ReaderView::onTranslated);
    if (service)
        connect(service, &core::TranslationService::asking, this, [this](int id, const QString& provider) {
            if (id == m_lookupId) m_translate->showAsking(provider);
        });
    if (service)
        connect(service, &core::TranslationService::interim, this, [this](int id, const core::TranslationResult& r) {
            if (id == m_lookupId) m_translate->showResult(r, m_dictionaryFirst, true);
        });
}

bool ReaderView::bookIsOffline() const
{
    return m_settings.offlineOnly || (m_bookId && m_settings.offlineBooks.contains(QString::number(m_bookId)));
}

void ReaderView::setBookOffline(bool on)
{
    if (!m_bookId) return;
    const QString id = QString::number(m_bookId);
    m_settings.offlineBooks.removeAll(id);
    if (on) m_settings.offlineBooks << id;
    emit appearanceChanged();   // the settings file needs saving
}

void ReaderView::translateSelection(bool dictionaryFirst)
{
    const QString text = m_pages->selectedText().trimmed();
    if (text.isEmpty()) return;
    m_note->hide();
    const QRect anchor = m_pages->rangeRect(m_pages->selectionSpine(), m_pages->selectionStart(), m_pages->selectionEnd());
    m_translate->setTheme(m_theme);
    m_translate->showLoading(text, languageCode(), anchor);
    if (!m_translator) {
        m_translate->showNothing(tr("Translation isn't available."));
        return;
    }
    core::TranslationRequest r;
    r.text = text.size() > 4000 ? text.left(4000) : text;
    r.sentence = m_pages->selectionSentence();
    if (r.sentence.size() > 600 || r.sentence == r.text) r.sentence.clear();
    r.from = languageCode();
    m_dictionaryFirst = dictionaryFirst;
    m_lookupSpine = m_pages->selectionSpine();
    m_lookupStart = m_pages->selectionStart();
    m_lookupEnd = m_pages->selectionEnd();
    m_lookupSentence = r.sentence.isEmpty() ? r.text : r.sentence;
    m_translator->setOfflineOnly(bookIsOffline());
    m_lookupId = m_translator->translate(r);
}

void ReaderView::onTranslated(int id, const core::TranslationResult& r, const QString& why)
{
    if (id != m_lookupId || !m_translate->isVisible()) return;
    if (r.isEmpty()) {
        QString text = why.isEmpty() ? tr("No translation service is set up.") : why;
        if (bookIsOffline()) text += QLatin1Char('\n') + tr("This book is kept offline, so Gemini isn't asked.");
        m_translate->showNothing(text);
    } else {
        m_translate->showResult(r, m_dictionaryFirst);
        // Every lookup goes to the word log, with the place it was seen.
        if (m_words && m_bookId) {
            core::WordPlace place;
            place.bookId = m_bookId;
            place.bookTitle = m_title->text();
            place.language = languageCode();
            place.spine = m_lookupSpine;
            place.start = m_lookupStart;
            place.end = m_lookupEnd;
            place.sentence = m_lookupSentence;
            const core::Position at{m_lookupSpine, m_lookupStart};
            place.cfi = m_engine->cfiFor(at);
            if (const auto ix = m_engine->index()) place.pageLabel = ix->pageLabel(at);
            place.chapter = chapterTitleAt(at);
            m_words->record(r, place);
            emit wordLogged();
        }
    }
}

QMenu* ReaderView::buildSelectionMenu()
{
    auto* menu = new QMenu(this);
    menu->setProperty("testId", "selectionMenu");
    const QString text = m_pages->selectedText().simplified();
    const int words = int(text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size());
    // Highlight colors in a row at the top, as in Kindle. Inside a highlight, the dot of its color is marked.
    if (m_notes && m_bookId) {
        const int current = highlightAtSelection();
        const QString currentColor = current ? m_notes->get(current).color : QString();
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(10, 6, 10, 6);
        h->setSpacing(10);
        for (const QString& c : core::Annotations::colors()) {
            auto* dot = new QToolButton;
            dot->setObjectName(QStringLiteral("Icon"));
            dot->setFixedSize(30, 30);
            dot->setCursor(Qt::PointingHandCursor);
            dot->setToolTip(tr("Highlight") + QStringLiteral(" (") + c + QLatin1Char(')'));
            dot->setProperty("testId", (QStringLiteral("hl-") + c).toLatin1().constData());
            QPixmap pm(44, 44);
            pm.setDevicePixelRatio(2.0);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(c == currentColor ? QPen(m_theme.fg, 2) : Qt::NoPen);
            p.setBrush(core::Annotations::colorValue(c));
            p.drawEllipse(QRectF(3, 3, 16, 16));
            p.end();
            dot->setIcon(QIcon(pm));
            dot->setIconSize(QSize(22, 22));
            h->addWidget(dot);
            connect(dot, &QToolButton::clicked, menu, [this, menu, c] {
                highlightSelection(c);
                menu->close();
            });
        }
        h->addStretch(1);
        auto* wa = new QWidgetAction(menu);
        wa->setDefaultWidget(row);
        wa->setProperty("testId", "menuHighlightRow");
        menu->addAction(wa);
        if (current) {
            QAction* remove = menu->addAction(tr("Remove Highlight"), this, [this] { removeHighlightAtSelection(); });
            remove->setProperty("testId", "menuRemoveHighlight");
        }
        menu->addSeparator();
    }
    QAction* translate = menu->addAction(tr("Translate"), this, [this] { translateSelection(false); });
    translate->setProperty("testId", "menuTranslate");
    QAction* dict = menu->addAction(tr("Look Up in Dictionary"), this, [this] { translateSelection(true); });
    dict->setProperty("testId", "menuDictionary");
    dict->setEnabled(words >= 1 && words <= 4);
    QAction* copy = menu->addAction(tr("Copy"), this, [this] {
        const QString t = m_pages->selectedText();
        if (!t.isEmpty()) QApplication::clipboard()->setText(t);
    });
    copy->setShortcut(QKeySequence::Copy);
    copy->setProperty("testId", "menuCopy");
    if (m_notes && m_bookId) {
        const int current = highlightAtSelection();
        const bool hasNote = current && !m_notes->get(current).note.isEmpty();
        QAction* note = menu->addAction(hasNote ? tr("Edit Note…") : tr("Add Note…"), this, [this] {
            int id = highlightAtSelection();
            if (!id) id = highlightSelection(QStringLiteral("yellow"));
            if (id) editNote(id);
        });
        note->setProperty("testId", "menuNote");
    }
    menu->addSeparator();
    QAction* search = menu->addAction(tr("Search This Book"), this, [this] { openSearch(m_pages->selectedText()); });
    search->setProperty("testId", "menuSearchBook");
    QAction* web = menu->addAction(tr("Search the Web"), this, [this] { searchWeb(false); });
    web->setProperty("testId", "menuWeb");
    QAction* wiki = menu->addAction(tr("Search Wikipedia"), this, [this] { searchWeb(true); });
    wiki->setProperty("testId", "menuWikipedia");
    return menu;
}

void ReaderView::searchWeb(bool wikipedia)
{
    const QString text = m_pages->selectedText().simplified().left(300);
    if (text.isEmpty()) return;
    QUrl url;
    QUrlQuery q;
    if (wikipedia) {
        // The book's own language edition: the words are in that language.
        const QString lang = languageCode().isEmpty() ? QStringLiteral("en") : languageCode();
        url = QUrl(QStringLiteral("https://%1.wikipedia.org/w/index.php").arg(lang));
        q.addQueryItem(QStringLiteral("search"), text);
    } else {
        url = QUrl(QStringLiteral("https://www.google.com/search"));
        q.addQueryItem(QStringLiteral("q"), text);
    }
    url.setQuery(q);
    m_lastUrl = url.toString(QUrl::FullyEncoded);
    if (m_interactive) QDesktopServices::openUrl(url);
}

void ReaderView::speak(const QString& text, const QString& language)
{
#ifdef HAVE_TEXT_TO_SPEECH
    if (!m_interactive || text.isEmpty()) return;
    if (!m_speech) m_speech = new QTextToSpeech(this);
    const QLocale want(language.isEmpty() ? QStringLiteral("en") : language);
    // A voice for the book's language when Windows has one; otherwise the default voice.
    for (const QLocale& l : m_speech->availableLocales())
        if (l.language() == want.language()) {
            m_speech->setLocale(l);
            break;
        }
    m_speech->say(text);
#else
    Q_UNUSED(text);
    Q_UNUSED(language);
#endif
}

// ---------------------------------------------------------------- highlights, notes, bookmarks

int ReaderView::highlightAtSelection() const
{
    if (!m_notes || !m_bookId || !m_pages->hasSelection()) return 0;
    const auto hits = m_notes->highlightsAt(m_bookId, m_pages->selectionSpine(), m_pages->selectionStart(), m_pages->selectionEnd());
    return hits.isEmpty() ? 0 : hits.first().id;
}

int ReaderView::highlightSelection(const QString& color)
{
    if (!m_notes || !m_bookId || !m_pages->hasSelection() || m_pages->selectedText().trimmed().isEmpty()) return 0;
    const int spine = m_pages->selectionSpine(), start = m_pages->selectionStart(), end = m_pages->selectionEnd();
    // Inside an existing highlight: that one changes color (a highlight is never stacked on another).
    for (core::Annotation a : m_notes->highlightsAt(m_bookId, spine, start, end))
        if (a.start <= start && a.end >= end) {
            a.color = color;
            m_notes->update(a);
            m_pages->clearSelection();
            refreshAnnotations();
            return a.id;
        }
    // Whole words, as in Kindle: a selection that starts or ends inside a word takes the whole word.
    int from = start, to = end;
    if (const auto layoutNow = m_engine->layout(spine)) {
        for (const layout::Block& b : layoutNow->flow->blocks) {
            if (b.kind != layout::Block::Text) continue;
            auto isWord = [&](int i) { return i >= 0 && i < int(b.text.size()) && (b.text[i].isLetterOrNumber() || b.text[i] == QChar(0x00AD)); };
            if (from >= b.start && from < b.start + b.length) {
                int i = from - b.start;
                while (i > 0 && isWord(i) && isWord(i - 1)) --i;
                from = b.start + i;
            }
            if (to > b.start && to <= b.start + b.length) {
                int i = to - b.start;
                while (i < int(b.text.size()) && isWord(i) && isWord(i - 1)) ++i;
                to = b.start + i;
            }
        }
    }
    // Overlapping ones are merged into the new highlight, keeping their notes.
    QStringList notes;
    for (const core::Annotation& a : m_notes->highlightsAt(m_bookId, spine, from, to)) {
        from = qMin(from, a.start);
        to = qMax(to, a.end);
        if (!a.note.trimmed().isEmpty()) notes << a.note.trimmed();
        m_notes->remove(a.id);
    }
    const auto cl = m_engine->layout(spine);
    core::Annotation a;
    a.bookId = m_bookId;
    a.kind = core::Annotation::Highlight;
    a.spine = spine;
    a.start = from;
    a.end = to;
    a.text = cl ? cl->flow->textRange(from, to).simplified() : m_pages->selectedText().simplified();
    a.color = color;
    a.note = notes.join(QStringLiteral("\n\n"));
    a.cfi = m_engine->cfiFor({spine, from});
    if (const auto ix = m_engine->index()) a.pageLabel = ix->pageLabel({spine, from});
    a.chapter = chapterTitleAt({spine, from});
    const int id = m_notes->add(a);
    m_translate->hide();
    m_pages->clearSelection();
    refreshAnnotations();
    return id;
}

bool ReaderView::removeHighlightAtSelection()
{
    const int id = highlightAtSelection();
    if (!id) return false;
    m_notes->remove(id);
    m_pages->clearSelection();
    refreshAnnotations();
    return true;
}

int ReaderView::addNoteToSelection(const QString& note)
{
    int id = highlightAtSelection();
    if (!id) id = highlightSelection(QStringLiteral("yellow"));
    if (!id) return 0;
    setNote(id, note);
    return id;
}

void ReaderView::setNote(int annotationId, const QString& note)
{
    if (!m_notes) return;
    core::Annotation a = m_notes->get(annotationId);
    if (a.id == 0) return;
    a.note = note.trimmed();
    m_notes->update(a);
    refreshAnnotations();
}

void ReaderView::editNote(int annotationId)
{
    if (!m_notes || !m_interactive) return;
    const core::Annotation a = m_notes->get(annotationId);
    if (a.id == 0) return;
    QDialog d(this);
    d.setWindowTitle(a.note.isEmpty() ? tr("Add Note") : tr("Edit Note"));
    auto* lay = new QVBoxLayout(&d);
    auto* quote = new QLabel(QStringLiteral("“%1”").arg(a.text.simplified().left(300)));
    quote->setWordWrap(true);
    quote->setObjectName(QStringLiteral("Muted"));
    lay->addWidget(quote);
    auto* edit = new QPlainTextEdit(a.note);
    edit->setMinimumSize(420, 160);
    lay->addWidget(edit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    edit->setFocus();
    if (d.exec() == QDialog::Accepted) setNote(annotationId, edit->toPlainText());
}

bool ReaderView::pageIsBookmarked() const
{
    if (!m_notes || !m_bookId) return false;
    const core::Position a = m_pages->position(), e = m_pages->screenEnd();
    return m_notes->bookmarkAt(m_bookId, a.spine, a.offset, e.spine == a.spine ? qMax(a.offset, e.offset - 1) : a.offset) != 0;
}

bool ReaderView::toggleBookmark()
{
    if (!m_notes || !m_bookId || !m_engine->isOpen()) return false;
    const core::Position a = m_pages->position(), e = m_pages->screenEnd();
    const int existing = m_notes->bookmarkAt(m_bookId, a.spine, a.offset, e.spine == a.spine ? qMax(a.offset, e.offset - 1) : a.offset);
    if (existing) {
        m_notes->remove(existing);
    } else {
        core::Annotation b;
        b.bookId = m_bookId;
        b.kind = core::Annotation::Bookmark;
        b.spine = a.spine;
        b.start = b.end = a.offset;
        if (const auto cl = m_engine->layout(a.spine)) b.text = cl->flow->textRange(a.offset, qMin(cl->flow->length, a.offset + 120)).simplified();
        b.cfi = m_engine->cfiFor(a);
        if (const auto ix = m_engine->index()) b.pageLabel = ix->pageLabel(a);
        b.chapter = chapterTitleAt(a);
        m_notes->add(b);
    }
    refreshAnnotations();
    return !existing;
}

void ReaderView::refreshAnnotations()
{
    const bool on = m_notes && m_bookId;
    const QList<core::Annotation> all = on ? m_notes->forBook(m_bookId) : QList<core::Annotation>();
    const int spine = m_pages->position().spine;
    std::vector<layout::Overlay> overlays;
    for (const core::Annotation& a : all) {
        if (a.kind != core::Annotation::Highlight || a.spine != spine) continue;
        QColor c = core::Annotations::colorValue(a.color);
        c.setAlphaF(m_theme.dark ? 0.34f : 0.42f);
        overlays.push_back({a.start, a.end, c});
    }
    m_pages->setHighlights(spine, overlays);
    m_notebook->setAnnotations(all);
    const QSignalBlocker b(m_bookmarkButton);
    m_bookmarkButton->setChecked(pageIsBookmarked());
    m_bookmarkButton->setToolTip(m_bookmarkButton->isChecked() ? tr("Remove the Bookmark (Ctrl+D)") : tr("Bookmark This Page (Ctrl+D)"));
}

void ReaderView::goToAnnotation(int annotationId)
{
    if (!m_notes) return;
    const core::Annotation a = m_notes->get(annotationId);
    if (a.id == 0 || a.bookId != m_bookId) return;
    core::Position p{a.spine, a.start};
    if (!a.cfi.isEmpty()) {
        core::Position fromCfi;
        if (m_engine->positionForCfi(a.cfi, &fromCfi)) p = fromCfi;
    }
    jumpTo(p);
    if (a.kind == core::Annotation::Highlight) m_pages->selectRange(p, {p.spine, p.offset + (a.end - a.start)});
    m_pages->setFocus();
}

bool ReaderView::exportNotebook(const QString& path, QString* error)
{
    if (!m_notes || !m_bookId) return false;
    const core::LibraryBook b = m_library ? m_library->book(m_bookId) : core::LibraryBook();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QString title = b.title.isEmpty() ? m_title->text() : b.title;
    // The file name chooses the format: .json for other tools, anything else Markdown.
    if (path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) f.write(m_notes->json(m_bookId, title, b.author, b.path));
    else f.write(m_notes->markdown(m_bookId, title, b.author).toUtf8());
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool ReaderView::notebookVisible() const { return m_notebook->isVisible(); }

void ReaderView::setNotebookVisible(bool on)
{
    m_notebook->setVisible(on);
    const QSignalBlocker b(m_notebookButton);
    m_notebookButton->setChecked(on);
    m_settings.showNotebook = on;
    if (on) refreshAnnotations();
    emit appearanceChanged();   // the settings file needs saving
}

// ---------------------------------------------------------------- search

void ReaderView::openSearch(const QString& query)
{
    if (!m_engine->isOpen()) return;
    m_toc->hide();
    {
        const QSignalBlocker b(m_tocButton);
        m_tocButton->setChecked(false);
    }
    m_search->show();
    {
        const QSignalBlocker b(m_searchButton);
        m_searchButton->setChecked(true);
    }
    if (!query.trimmed().isEmpty()) m_search->setQuery(query);
    m_search->focusBox();
}

void ReaderView::closeSearch()
{
    m_engine->cancelSearch();
    m_search->hide();
    const QSignalBlocker b(m_searchButton);
    m_searchButton->setChecked(false);
    if (m_settings.showContents && m_engine->isOpen()) {
        m_toc->show();
        const QSignalBlocker b2(m_tocButton);
        m_tocButton->setChecked(true);
    }
}

void ReaderView::goToSearchHit(int index)
{
    const QList<core::SearchHit>& hits = m_search->results();
    if (index < 0 || index >= hits.size()) return;
    const core::SearchHit& h = hits[index];
    jumpTo({h.spine, h.start});
    m_pages->selectRange({h.spine, h.start}, {h.spine, h.end});
}

void ReaderView::showAppearancePanel()
{
    if (!m_panel->isVisible()) m_aaButton->click();
}

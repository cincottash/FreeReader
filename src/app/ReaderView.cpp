#include "ReaderView.h"

#include "AppearancePanel.h"
#include "FooterBar.h"
#include "Icons.h"
#include "Popups.h"
#include "Settings.h"
#include "TocPanel.h"
#include "NotebookPanel.h"
#include "BilingualPanel.h"
#include "PracticePanel.h"
#include "PracticeView.h"

#include <QCryptographicHash>
#include "SearchPanel.h"
#include "TranslationPopup.h"
#include "DictionaryPopup.h"

#include "core/services/BookEngine.h"
#include "core/services/Library.h"
#include "core/translate/TranslationService.h"
#include <map>
#include <QTextBoundaryFinder>
#include <QSqlDatabase>
#include "core/layout/Flow.h"
#include "core/services/KindleImport.h"
#include "core/services/KindleApply.h"
#include "core/translate/OfflineTranslator.h"
#include "core/translate/OfflineDictionary.h"
#include "core/translate/DictionaryCardHtml.h"
#include "core/translate/LanguagePacks.h"
#include "core/services/BookIndex.h"
#include "core/services/WordLog.h"
#include "core/services/Statistics.h"
#include "core/services/Annotations.h"
#include "core/services/Highlighting.h"

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
#include <QHash>
#include "ElidedLabel.h"
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
#include <limits>

namespace {

// The name on the card's side in the book's language, written in that language (the side is all in it).
QString bookDictionaryTitle(const QString& book)
{
    if (book == QLatin1String("es")) return QStringLiteral("Diccionario de español");
    if (book == QLatin1String("en")) return QStringLiteral("English Dictionary");
    return ReaderView::tr("%1 Dictionary").arg(core::languages::displayName(book));
}

// The word's entry on Wiktionary: the `edition` in the reader's language, at the book language's section.
QUrl wiktionaryUrl(const core::DictionaryCard& card, const QString& edition, const QString& book)
{
    if (card.isEmpty()) return {};
    QUrl u(QStringLiteral("https://%1.wiktionary.org/wiki/").arg(edition) +
           QString(card.headwords.first().word).replace(QLatin1Char(' '), QLatin1Char('_')));
    if (edition != book) u.setFragment(QLocale::languageToString(QLocale(book).language()));
    return u;
}

// The side in the book's language when nothing has the word in it: says why, and offers the download where a
// dictionary of definitions exists for that language.
DictionaryPopup::Side missingDefinitions(const QList<core::OfflineDictionary*>& dictionaries, const QString& book, bool dictionariesOn)
{
    DictionaryPopup::Side s;
    s.language = book;
    s.title = bookDictionaryTitle(book);
    bool installed = false;
    for (const core::OfflineDictionary* d : dictionaries)
        if (d && d->info().ok && d->isMonolingual() && d->info().language == book) installed = true;
    const QString name = core::languages::displayName(book);
    s.missing = !dictionariesOn ? ReaderView::tr("The offline dictionaries are turned off (Settings > Translation).")
                : installed     ? ReaderView::tr("The %1 definitions don't have this word.").arg(name)
                                : ReaderView::tr("The %1 definitions aren't installed.").arg(name);
    s.offerDefinitions = dictionariesOn && !installed && (book == QLatin1String("es") || book == QLatin1String("en"));
    return s;
}

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
    // Shortened with "…" when the header has no room (a long title never widens the window).
    m_title = new ElidedLabel;
    m_title->setObjectName(QStringLiteral("Title"));
    m_subtitle = new ElidedLabel;
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
    h->addWidget(m_aaButton);
    // One page or two side by side, like an open book (the Columns setting, One or Two).
    m_spreadButton = iconButton("spread", tr("Two Pages Side by Side"), m_theme);
    m_spreadButton->setCheckable(true);
    m_spreadButton->setProperty("testId", "spreadButton");
    h->addWidget(m_spreadButton);
    // Recently used themes as dots (as in the mock-up), then the full theme list.
    m_themeDots = new QWidget;
    m_themeDots->setLayout(new QHBoxLayout);
    m_themeDots->layout()->setContentsMargins(6, 0, 6, 0);
    m_themeDots->layout()->setSpacing(0);
    h->addWidget(m_themeDots);
    h->addWidget(m_bookmarkButton);
    // The bookmark added last, in any book (the owner's request, 2026-11-25): beside Bookmark This Page.
    m_latestBookmarkButton = iconButton("bookmarkGo", tr("Go to Latest Bookmark (Ctrl+Shift+D)"), m_theme);
    m_latestBookmarkButton->setProperty("testId", "latestBookmarkButton");
    h->addWidget(m_latestBookmarkButton);
    connect(m_latestBookmarkButton, &QToolButton::clicked, this, &ReaderView::latestBookmarkRequested);
    h->addWidget(m_notebookButton);
    // Practice: a button with its name, easy to find (the owner's request, 2026-11-23).
    m_practiceButton = new QToolButton;
    m_practiceButton->setObjectName(QStringLiteral("Practice"));
    m_practiceButton->setText(tr("Practice"));
    m_practiceButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_practiceButton->setIcon(icons::icon("cards", m_theme.fg));
    m_practiceButton->setIconSize(QSize(20, 20));
    m_practiceButton->setFixedHeight(36);
    m_practiceButton->setCheckable(true);
    m_practiceButton->setCursor(Qt::PointingHandCursor);
    m_practiceButton->setFocusPolicy(Qt::NoFocus);
    m_practiceButton->setToolTip(tr("Practice beside the book: flashcards, and the words of this page or book with their meanings (Ctrl+P)"));
    m_practiceButton->setProperty("testId", "practiceButton");
    h->addWidget(m_practiceButton);
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
    m_bilingual = new BilingualPanel;
    m_bilingual->setFixedWidth(380);
    m_bilingual->hide();
    body->addWidget(m_bilingual);
    m_practicePanel = new PracticePanel;
    m_practicePanel->setFixedWidth(qBound(360, m_settings.practiceWidth, 1600));
    // Its left edge is dragged to make it wider or narrower (the owner's request, 2026-11-25): at least 360 px, and
    // the page keeps at least 360 px beside it (and the contents or search panel, when open).
    // While dragging, the panel follows the mouse and so does the book's text, one layout at a time (PageView::
    // setHoldLayout); a guide line with the panel changing only on release didn't look like resizing, and text kept
    // as it was stopped following once the page was narrower than its column (the owner's reports, 2026-11-25/26).
    auto clampWidth = [this](int wanted) {
        const int left = m_toc->isVisible() || m_search->isVisible() ? 320 : 0;
        const int least = m_practicePanel->narrowest();
        return qBound(least, wanted, qMax(least, width() - left - 360));
    };
    connect(m_practicePanel, &PracticePanel::widthDragging, this, [this, clampWidth](int wanted) {
        m_pages->setHoldLayout(true);
        m_practicePanel->setFixedWidth(clampWidth(wanted));
        // Placed now, not when the layout's posted request comes: on Windows that waits while the mouse keeps
        // moving, and a panel made wider only grew past the window's right edge until the button was released
        // (narrower, it visibly shrank in place, so that way looked live: the owner's report, 2026-11-26).
        if (layout()) layout()->activate();
    });
    connect(m_practicePanel, &PracticePanel::widthDragged, this, [this, clampWidth](int wanted) {
        const int w = clampWidth(wanted);
        m_practicePanel->setFixedWidth(w);
        if (layout()) layout()->activate();   // the page gets its final size now, before the hold ends
        m_pages->setHoldLayout(false);
        if (w == m_settings.practiceWidth) return;
        m_settings.practiceWidth = w;
        emit appearanceChanged();   // the settings file needs saving
    });
    m_practicePanel->hide();
    m_practicePanel->setProperty("testId", "practicePanel");
    body->addWidget(m_practicePanel);
    connect(m_practicePanel, &PracticePanel::closeRequested, this, [this] {
        setPracticeVisible(false);
        m_pages->setFocus();
    });
    // Maximize: the panel over the page area, in place (its button then says Restore, in the same spot). Before, it
    // opened Practice in the whole window, whose way back sat elsewhere (the owner's report, 2026-11-25).
    connect(m_practicePanel, &PracticePanel::fullWindowRequested, this, [this] { setPracticeMaximized(!m_practiceMaximized); });
    m_bilingualTimer.setSingleShot(true);
    m_bilingualTimer.setInterval(250);   // a quick run of page turns asks once, for the page it stops on
    connect(&m_bilingualTimer, &QTimer::timeout, this, &ReaderView::refreshBilingual);
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
    m_dictCard = new DictionaryPopup(m_pages);
    connect(m_dictCard, &DictionaryPopup::translateRequested, this, [this] { translateSelection(false); });
    // Highlight from the pop-ups, without the right-click menu.
    connect(m_translate, &TranslationPopup::highlightRequested, this, &ReaderView::toggleLookupHighlight);
    connect(m_dictCard, &DictionaryPopup::highlightRequested, this, &ReaderView::toggleLookupHighlight);
    connect(m_dictCard, &DictionaryPopup::settingsRequested, this, &ReaderView::translationSettingsRequested);
    connect(m_dictCard, &DictionaryPopup::fullDefinitionRequested, this, [this](const QUrl& url) {
        m_lastUrl = url.toString(QUrl::FullyEncoded);
        if (m_interactive) QDesktopServices::openUrl(url);
    });
    auto speechSettings = [this] {
        // Windows' own page for adding voices.
        m_lastUrl = QStringLiteral("ms-settings:speech");
        if (m_interactive) QDesktopServices::openUrl(QUrl(m_lastUrl));
    };
    connect(m_translate, &TranslationPopup::speechSettingsRequested, this, speechSettings);
    connect(m_dictCard, &DictionaryPopup::speechSettingsRequested, this, speechSettings);
    connect(m_dictCard, &DictionaryPopup::speakRequested, this, &ReaderView::speak);
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
    // Page turns: for all books, saved with the settings.
    m_panel->setPageTurn(m_settings.pageTurn, m_settings.pageTurnMs, m_settings.pageTurnMotion);
    m_pages->setPageTurn(m_settings.pageTurn, m_settings.pageTurnMs, m_settings.pageTurnMotion);
    connect(m_panel, &AppearancePanel::pageTurnChanged, this, [this](const QString& style, int ms, const QString& motion) {
        m_settings.pageTurn = style;
        m_settings.pageTurnMs = ms;
        m_settings.pageTurnMotion = motion;
        m_pages->setPageTurn(style, ms, motion);
        emit appearanceChanged();
    });
    applyPageTurnSettings();
    connect(m_panel, &AppearancePanel::turnArrowsChanged, this, [this](bool on) {
        m_settings.pageTurnArrows = on;
        m_pages->setTurnArrows(on);
        emit appearanceChanged();
    });
    connect(m_pages, &PageView::zoomRequested, this, [this](int steps) { stepTextSize(steps * 10); });
    connect(m_pages, &PageView::marginsRequested, this, [this](int steps) { stepMargins(steps * 16); });
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
    connect(m_pages, &PageView::pageTurnedByUser, m_dictCard, &QWidget::hide);
    connect(m_searchButton, &QToolButton::toggled, this, [this](bool on) { on ? openSearch() : closeSearch(); });
    connect(m_bookmarkButton, &QToolButton::clicked, this, [this] {
        toggleBookmark();
        m_pages->setFocus();
    });
    connect(m_notebookButton, &QToolButton::toggled, this, [this](bool on) { setNotebookVisible(on); });
    connect(m_practiceButton, &QToolButton::clicked, this, [this](bool on) { setPracticeVisible(on); });
    connect(m_pages, &PageView::columnsChanged, this, &ReaderView::syncSpreadButton);
    connect(m_spreadButton, &QToolButton::clicked, this, [this] {
        toggleTwoPages();
        m_pages->setFocus();
    });
    // New words (a lookup, a highlight) show in the workspace at once.
    connect(this, &ReaderView::wordLogged, this, [this] {
        if (practiceVisible() && m_practicePanel->view()) m_practicePanel->view()->refresh();
    });
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
        // Every format here can be imported again (File > Import Highlights…).
        const QStringList filters = {tr("Markdown (*.md)"), tr("Plain Text (*.txt)"), tr("CSV, Comma-Separated (*.csv)"),
                                     tr("TSV, Tab-Separated (*.tsv)"), tr("JSON (*.json)")};
        QString chosen = filters.first();
        QString path = QFileDialog::getSaveFileName(this, tr("Export Notebook"),
                                                    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') + name + QStringLiteral(" notes.md"),
                                                    filters.join(QStringLiteral(";;")), &chosen);
        if (path.isEmpty()) return;
        // The format chosen in the list wins over the name's ending.
        static const QRegularExpression extRe(QStringLiteral("\\*(\\.\\w+)\\)"));
        const QString ext = extRe.match(chosen).captured(1);
        const QFileInfo fi(path);
        if (!ext.isEmpty() && QLatin1Char('.') + fi.suffix().toLower() != ext) {
            static const QStringList known = {QStringLiteral("md"), QStringLiteral("txt"), QStringLiteral("csv"), QStringLiteral("tsv"), QStringLiteral("json")};
            path = (known.contains(fi.suffix().toLower()) ? fi.path() + QLatin1Char('/') + fi.completeBaseName() : path) + ext;
        }
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
    // A click on the page closes them even when nothing is selected (after a highlight made with the translation
    // open, the selection is gone but the translation stays: the owner's bug report, 2026-11-23).
    connect(m_pages, &PageView::pageClicked, this, [this] {
        m_translate->hide();
        m_dictCard->hide();
    });
    connect(m_pages, &PageView::selectionCleared, this, [this] {
        if (m_keepPopup) return;
        m_translate->hide();
        m_dictCard->hide();
    });
    connect(m_pages, &PageView::resized, m_translate, &QWidget::hide);
    connect(m_pages, &PageView::resized, m_dictCard, &QWidget::hide);
    connect(m_pages, &PageView::resized, m_note, &QWidget::hide);
    // The Back chip stays at the bottom center when a panel opens or the window changes size.
    connect(m_pages, &PageView::resized, this, [this] {
        if (m_backChip->isVisible()) placeBackChip();
    });
    connect(m_pages, &PageView::selectionMade, this, [this] {
        // A slip of the mouse that selects a letter or two, or only punctuation, isn't worth a lookup.
        const QString t = m_pages->selectedText();
        int letters = 0;
        for (const QChar c : t)
            if (c.isLetter()) ++letters;
        if (m_settings.translateOnSelect && letters >= 2) translateSelection();
    });
    connect(m_pages, &PageView::selectionMenuRequested, this, [this](const QPoint& at) {
        QMenu* menu = buildSelectionMenu();
        if (m_interactive) menu->exec(at);
        delete menu;
    });
    connect(m_engine, &core::BookEngine::indexReady, this, &ReaderView::onIndexReady);
    connect(m_footer, &FooterBar::statClicked, this, [this] {
        m_settings.footerPagesLeft = !m_settings.footerPagesLeft;
        updateHeaderAndFooter();
        emit appearanceChanged();   // the settings file needs saving
    });
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
    m_latestBookmarkButton->setIcon(icons::icon("bookmarkGo", t.fg));
    m_notebookButton->setIcon(icons::icon("panel", t.fg));
    m_spreadButton->setIcon(icons::icon("spread", t.fg));
    m_practiceButton->setIcon(icons::icon("cards", t.fg));
    m_notebook->setTheme(t);
    m_bilingual->setTheme(t);
    m_practicePanel->setTheme(t);
    setFullScreenIcon(window() && window()->isFullScreen());
    m_pages->setTheme(t);
    m_translate->setTheme(t);
    m_dictCard->setTheme(t);
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
    // One Themes button (the recent themes as dots here confused more than they helped: the owner's request,
    // 2026-11-22).
    auto* more = new QToolButton;
    more->setObjectName(QStringLiteral("Icon"));
    more->setFixedSize(32, 40);
    more->setCursor(Qt::PointingHandCursor);
    more->setFocusPolicy(Qt::NoFocus);
    more->setIcon(icons::icon("theme", m_theme.fg));
    more->setToolTip(tr("Themes (Ctrl+Shift+T)"));
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
    return loc.language() == QLocale::C ? QString() : core::languages::displayName(code);
}

Appearance ReaderView::effectiveAppearance() const
{
    Appearance a = m_settings.look;
    const core::Book* book = m_engine->book();
    if (book && book->metadata.pictures) {
        // Comics and scanned pages: the picture as large as the window allows (thin margins, no line length), one
        // page at a time (each page is a chapter of its own, so a second column would stay empty).
        a.marginPx = 12;
        a.pageWidthEm = 80;
        a.columns = QStringLiteral("one");
        if (m_library && m_bookId) a.merge(m_library->appearanceLayer(QStringLiteral("book:%1").arg(m_bookId)));
        return a;
    }
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
    if (m_bookId && m_bookId != bookId) {
        creditReading(false, 0);
        flushReading();
    }
    m_bookId = bookId;
    // Reading time starts now (counted at each page turn, in gaps of at most 5 minutes).
    m_readingMark = readingNow();
    m_pendingReadMs = 0;
    m_pendingPages = 0;
    m_pendingChars = 0;
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
    setBilingualVisible(m_settings.showBilingual);
    if (PracticeView* v = m_practicePanel->view()) {
        v->endSession();   // a session from another book doesn't carry over
        v->setOpenBook(bookId, book ? book->metadata.title : QString());
    }
    setPracticeVisible(m_settings.showPractice);
    closeSearch();
    applyReadingOptions();
    m_pages->setRightToLeft(pagesRightToLeft());
    m_lastShown = start;
    m_sinceTurn.start();
    m_pages->setPosition(start);
    if (m_engine->index()) onIndexReady();
    updateHeaderAndFooter();
    m_pages->setFocus();
}

void ReaderView::closeBook()
{
    if (m_bookId) {
        creditReading(false, 0);
        savePosition();   // writes the reading time too
    }
    m_readingMark = 0;
    m_saveTimer.stop();
    m_bookId = 0;
    m_note->hide();
    m_translate->hide();
    m_dictCard->hide();
    m_lookupId = 0;
    m_engine->cancelSearch();
    m_search->setResults({}, {}, false);
    m_notebook->setAnnotations({});
    m_pages->setHighlights(-1, {});
}

bool ReaderView::contentsVisible() const { return m_toc->isVisible(); }

void ReaderView::makeRoom(bool right, int width)
{
    // A panel on each side and the page (at least 360 px) must fit; when they don't, the side opened last stays.
    // Both at once in a narrow window made the window wider than the screen (2026-11-24).
    const int page = 360;
    if (right) {
        if ((m_toc->isVisible() || m_search->isVisible()) && 320 + width + page > this->width()) {
            if (m_search->isVisible()) closeSearch();
            if (m_toc->isVisible()) setContentsVisible(false);
        }
        return;
    }
    const int other = m_notebook->isVisible() ? 340 : m_bilingual->isVisible() ? 380 : m_practicePanel->isVisible() ? m_practicePanel->width() : 0;
    if (other && width + other + page > this->width()) {
        if (m_notebook->isVisible()) setNotebookVisible(false);
        if (m_bilingual->isVisible()) setBilingualVisible(false);
        if (m_practicePanel->isVisible()) setPracticeVisible(false);
    }
}

void ReaderView::setContentsVisible(bool on)
{
    if (on && m_search->isVisible()) closeSearch();
    if (on) makeRoom(false, 320);
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
        if (loc.language() != QLocale::C) sub << core::languages::displayTitle(book->metadata.language);
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
    // What is left of the chapter: the time (from the reading speed learned from page turns) or the pages (in the
    // numbering "Page X of Y" uses); a click on it switches between the two.
    stats << core::chapterLeftText(*ix, pos, m_settings.readingSpeed, m_settings.footerPagesLeft);
    m_footer->setProgress(fraction, stats);
    m_footer->setClickableStat(int(stats.size()) - 1, m_settings.footerPagesLeft ? tr("Click to show the time left in the chapter")
                                                                                  : tr("Click to show the pages left in the chapter"));
    syncSpreadButton();
    // The workspace's This Page follows the pages showing.
    if (practiceVisible()) {
        updatePracticePage();
        PracticeView* v = m_practicePanel->view();
        if (v->page() == PracticeView::ListPage && v->listBook() == PracticeView::kThisPage) v->refresh();
    }
}

void ReaderView::onPositionChanged(const core::Position& p, bool byTurn)
{
    // Another screen: what was open beside the words of the last one closes.
    if (p != m_lastShown) {
        m_translate->hide();
        m_dictCard->hide();
    }
    // Learn the reading speed from forward page turns at a plausible pace.
    if (byTurn && m_lastShown < p && m_lastScreenChars > 200) {
        const double seconds = m_sinceTurn.elapsed() / 1000.0;
        if (seconds > 3 && seconds < 600) {
            const double cpm = m_lastScreenChars / (seconds / 60.0);
            m_settings.readingSpeed = qBound(200, int(0.85 * m_settings.readingSpeed + 0.15 * cpm), 6000);
        }
    }
    // Reading statistics: the time on the page just left, and a page read when it was a forward turn.
    creditReading(byTurn && m_lastShown < p, m_lastScreenChars);
    if (m_bilingual->isVisible()) m_bilingualTimer.start();
    m_sinceTurn.restart();
    m_lastShown = p;
    const core::Position end = m_pages->screenEnd();
    m_lastScreenChars = end.spine == p.spine ? qMax(0, end.offset - p.offset) : 0;
    updateHeaderAndFooter();
    if (m_bookId) m_saveTimer.start();
    refreshAnnotations();
}

ReaderView::~ReaderView() = default;

qint64 ReaderView::readingNow() const { return m_readingClock ? m_readingClock() : QDateTime::currentMSecsSinceEpoch(); }

void ReaderView::creditReading(bool forwardTurn, int chars)
{
    if (!m_bookId) return;
    const qint64 now = readingNow();
    // The time since the last turn, while the book was on screen; a long pause (the reader away) counts 5 minutes.
    if (m_readingMark > 0 && now > m_readingMark && isVisible()) m_pendingReadMs += qMin<qint64>(now - m_readingMark, 5 * 60 * 1000);
    m_readingMark = now;
    if (forwardTurn) {
        ++m_pendingPages;
        m_pendingChars += qMax(0, chars);
    }
}

void ReaderView::flushReading()
{
    if (!m_bookId || !m_library || (m_pendingReadMs < 1000 && m_pendingPages == 0)) return;
    if (!m_readingLog) m_readingLog = std::make_unique<core::ReadingLog>(m_library);
    const int seconds = int(m_pendingReadMs / 1000);
    m_readingLog->add(m_bookId, QDate::currentDate(), seconds, m_pendingPages, m_pendingChars);
    m_pendingReadMs -= qint64(seconds) * 1000;
    m_pendingPages = 0;
    m_pendingChars = 0;
}

void ReaderView::savePosition()
{
    flushReading();
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
}

bool ReaderView::pagesRightToLeft() const
{
    const QString key = QString::number(m_bookId) + QLatin1Char('=');
    for (const QString& d : m_settings.pageDirections)
        if (d.startsWith(key)) return d.mid(key.size()) == QLatin1String("rtl");
    const core::Book* book = m_engine->book();
    return book && book->metadata.rightToLeft;
}

void ReaderView::setPagesRightToLeft(bool on)
{
    if (!m_bookId) return;
    const QString key = QString::number(m_bookId) + QLatin1Char('=');
    for (int i = int(m_settings.pageDirections.size()) - 1; i >= 0; --i)
        if (m_settings.pageDirections[i].startsWith(key)) m_settings.pageDirections.removeAt(i);
    const core::Book* book = m_engine->book();
    // Only a choice that differs from the book's own is kept.
    if (!book || book->metadata.rightToLeft != on) m_settings.pageDirections << key + (on ? QStringLiteral("rtl") : QStringLiteral("ltr"));
    m_pages->setRightToLeft(on);
    applyReadingOptions();   // a comic shows one page at a time right to left
    m_pages->showNotice(on ? tr("Pages turn right to left: the left edge and the Left key go forward.") : tr("Pages turn left to right."));
}

bool ReaderView::bookIsOffline() const
{
    return m_bookId && m_settings.offlineBooks.contains(QString::number(m_bookId));
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
    m_dictCard->hide();
    const QRect anchor = m_pages->rangeRect(m_pages->selectionSpine(), m_pages->selectionStart(), m_pages->selectionEnd());
    m_lookupAnchor = anchor;
    m_lookupText = text;
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
    m_lookupKind = core::TranslationRequest::Kind::Lookup;
    m_lookupIsWord = r.isWord();
    m_translator->setOfflineOnly(bookIsOffline());
    m_lookupId = m_translator->translate(r);
}

void ReaderView::showDictionary()
{
    const QString text = m_pages->selectedText().trimmed();
    if (text.isEmpty()) return;
    // No dictionary knows the word (or none is on): the translation pop-up with its definition first.
    const core::FoundCard f = m_settings.useDictionary ? core::findDictionaryCard(m_dictionaries, text, languageCode()) : core::FoundCard();
    if (!f.found()) {
        translateSelection(true);
        return;
    }
    m_note->hide();
    m_translate->hide();
    const QRect anchor = m_pages->rangeRect(m_pages->selectionSpine(), m_pages->selectionStart(), m_pages->selectionEnd());
    // The card's words, for its Highlight button.
    m_lookupSpine = m_pages->selectionSpine();
    m_lookupStart = m_pages->selectionStart();
    m_lookupEnd = m_pages->selectionEnd();
    m_dictCard->setTheme(m_theme);
    m_dictCard->setTranslateLink(true);
    m_dictCard->setSpeech(text, languageCode());
    updateLookupHighlight();
    // A book in another language: two sides, the book's language first, then the reader's (Flip turns it over).
    // Flip is there even when no dictionary of definitions has the word: that side says why (the owner's request,
    // 2026-11-25).
    const core::FoundCard defs = core::findDictionaryCard(m_dictionaries, text, languageCode(), core::DictionaryKind::Definitions);
    const core::FoundCard trans = core::findDictionaryCard(m_dictionaries, text, languageCode(), core::DictionaryKind::Translating);
    if (trans.found() && trans.dictionary->info().target != languageCode()) {
        const DictionaryPopup::Side reader{trans.card, trans.title, trans.source, trans.fullDefinition, trans.dictionary->info().target};
        if (defs.found())
            m_dictCard->showSides({defs.card, defs.title, defs.source, defs.fullDefinition, languageCode()}, reader, anchor);
        else
            m_dictCard->showSides(reader, missingDefinitions(m_dictionaries, languageCode(), true), anchor);
        return;
    }
    m_dictCard->showCard(f.card, f.title, f.source, f.fullDefinition, anchor, false);
}

void ReaderView::showWordCardForSelection(const core::TranslationResult& r, bool dictionaryFirst)
{
    m_lookupAnchor = m_pages->rangeRect(m_pages->selectionSpine(), m_pages->selectionStart(), m_pages->selectionEnd());
    m_lookupText = m_pages->selectedText().trimmed();
    m_lookupSpine = m_pages->selectionSpine();
    m_lookupStart = m_pages->selectionStart();
    m_lookupEnd = m_pages->selectionEnd();
    m_dictionaryFirst = dictionaryFirst;
    showWordCard(r);
    updateLookupHighlight();
}

void ReaderView::showWordCard(const core::TranslationResult& r)
{
    const QString book = languageCode();
    const QString mine = core::languages::readerLanguage();
    const QString text = r.text.isEmpty() ? m_lookupText : r.text;
    QString source = tr("Source: %1").arg(r.provider.isEmpty() ? tr("unknown") : r.provider);
    if (r.fromCache) source += tr(" (looked up before)");
    // The side in the reader's language: the offline dictionary's own entry when it answered, else the engine's
    // card, else one made from the answer's meanings.
    DictionaryPopup::Side front;
    front.language = mine;
    if (r.provider == core::OfflineDictionary::providerName()) {
        const core::FoundCard t = core::findDictionaryCard(m_dictionaries, text, book,
                                                           book == mine ? core::DictionaryKind::Definitions : core::DictionaryKind::Translating);
        if (t.found()) front = {t.card, t.title, t.source, t.fullDefinition, mine};
    }
    if (front.card.isEmpty()) {
        front.card = !r.card.isEmpty() ? r.card : DictionaryPopup::cardFromResult(r, false, book);
        front.title = book == mine ? tr("%1 Dictionary").arg(core::languages::displayName(book))
                                   : tr("%1–%2 Dictionary").arg(core::languages::displayName(book), core::languages::displayName(mine));
        front.source = source;
        front.fullDefinition = wiktionaryUrl(front.card, mine, book);
    }
    if (front.card.isEmpty()) {   // nothing to make a card of (an answer with no meaning): the pop-up says so
        m_translate->showResult(r, m_dictionaryFirst);
        return;
    }
    // The translation pop-up keeps the answer (its lookup is over) but stays hidden: the card shows it.
    m_translate->showResult(r, m_dictionaryFirst);
    m_translate->hide();
    m_dictCard->setTheme(m_theme);
    m_dictCard->setTranslateLink(false);   // this is the translation
    m_dictCard->setSpeech(text, book);
    if (book == mine) {
        m_dictCard->showCard(front.card, front.title, front.source, front.fullDefinition, m_lookupAnchor, false);
        return;
    }
    // The side in the book's language: the engine's, else the dictionary of definitions', else one made from the
    // answer's definition, else what is missing.
    DictionaryPopup::Side back;
    back.language = book;
    if (!r.bookCard.isEmpty()) {
        back = {r.bookCard, bookDictionaryTitle(book), source, wiktionaryUrl(r.bookCard, book, book), book};
    } else if (const core::FoundCard d = m_settings.useDictionary ? core::findDictionaryCard(m_dictionaries, text, book, core::DictionaryKind::Definitions)
                                                                  : core::FoundCard();
               d.found()) {
        back = {d.card, d.title, d.source, d.fullDefinition, book};
    } else if (const core::DictionaryCard made = DictionaryPopup::cardFromResult(r, true, book); !made.isEmpty()) {
        back = {made, bookDictionaryTitle(book), source, wiktionaryUrl(made, book, book), book};
    } else {
        back = missingDefinitions(m_dictionaries, book, m_settings.useDictionary);
    }
    // Translate: the reader's language first; Dictionary (no offline entry): the book's.
    if (m_dictionaryFirst && !back.card.isEmpty()) m_dictCard->showSides(back, front, m_lookupAnchor);
    else m_dictCard->showSides(front, back, m_lookupAnchor);
}

void ReaderView::updateLookupHighlight()
{
    const bool can = m_notes && m_bookId && m_lookupSpine >= 0 && m_lookupEnd > m_lookupStart;
    const QList<core::Annotation> hits = can ? m_notes->highlightsAt(m_bookId, m_lookupSpine, m_lookupStart, m_lookupEnd) : QList<core::Annotation>();
    const QString next = core::Annotations::colorsInOrder(m_settings.highlightOrder).value(0, QStringLiteral("yellow"));
    const QColor color = core::Annotations::colorValue(hits.isEmpty() ? next : hits.first().color);
    // An explanation is about a sentence, not words to keep: no Highlight there.
    if (m_lookupKind != core::TranslationRequest::Kind::Explain) m_translate->setHighlight(can, !hits.isEmpty(), color);
    m_dictCard->setHighlight(can, !hits.isEmpty(), color);
}

void ReaderView::toggleLookupHighlight()
{
    if (!m_notes || !m_bookId || m_lookupSpine < 0 || m_lookupEnd <= m_lookupStart) return;
    const QList<core::Annotation> hits = m_notes->highlightsAt(m_bookId, m_lookupSpine, m_lookupStart, m_lookupEnd);
    if (!hits.isEmpty()) {
        m_notes->remove(hits.first().id);
        refreshAnnotations();
    } else {
        // The looked-up words again (the selection may be gone), highlighted in the color used last.
        m_keepPopup = true;
        m_pages->selectRange({m_lookupSpine, m_lookupStart}, {m_lookupSpine, m_lookupEnd});
        m_keepPopup = false;
        highlightSelection(core::Annotations::colorsInOrder(m_settings.highlightOrder).value(0, QStringLiteral("yellow")));
    }
    updateLookupHighlight();
}

bool ReaderView::geminiForExplainOnly() const
{
    return m_settings.useGemini && m_settings.geminiExplainOnly;
}

void ReaderView::explainSelection()
{
    if (!m_pages->hasSelection()) return;
    // The sentence the selection is in; a selection longer than one sentence is explained as it is.
    QString text = m_pages->selectionSentence().simplified();
    const QString selected = m_pages->selectedText().simplified();
    if (selected.size() > text.size()) text = selected;
    if (text.isEmpty()) return;
    if (text.size() > 1500) text = text.left(1500);
    m_note->hide();
    m_dictCard->hide();
    const QRect anchor = m_pages->rangeRect(m_pages->selectionSpine(), m_pages->selectionStart(), m_pages->selectionEnd());
    m_translate->setTheme(m_theme);
    m_translate->showLoading(text, languageCode(), anchor);
    if (!m_translator) {
        m_translate->showNothing(tr("Translation isn't available."));
        return;
    }
    core::TranslationRequest r;
    r.kind = core::TranslationRequest::Kind::Explain;
    r.text = text;
    r.sentence = text;
    r.from = languageCode();
    m_lookupKind = r.kind;
    m_translator->setOfflineOnly(bookIsOffline());
    m_lookupId = m_translator->translate(r);
}

void ReaderView::onTranslated(int id, const core::TranslationResult& r, const QString& why)
{
    if (m_bilingualAsked.contains(id)) {
        onBilingualTranslated(id, r, why);
        return;
    }
    if (id != m_lookupId || !m_translate->isVisible()) return;
    if (r.isEmpty()) {
        const bool allOff = !m_settings.useGemini && !m_settings.useLmStudio && !m_settings.useDictionary && !m_settings.useOfflineTranslator;
        QString text = allOff ? tr("Every translation engine is turned off (Settings > Translation).")
                              : why.isEmpty() ? tr("No translation engine is set up yet.") : why;
        if (bookIsOffline() && m_settings.useGemini) text += QLatin1Char('\n') + tr("This book is kept offline, so Gemini isn't asked.");
        else if (geminiForExplainOnly() && m_lookupKind != core::TranslationRequest::Kind::Explain)
            text += QLatin1Char('\n') + tr("Gemini is used only for Explain This Sentence (Settings > Translation).");
        // Nothing answered, and a free download would have: say where to get it (the dictionary for a word, the
        // offline translator for Spanish sentences).
        const bool lookup = m_lookupKind == core::TranslationRequest::Kind::Lookup;
        const QString mine = core::languages::readerLanguage();
        // A pack exists for this book's language: its dictionary and its translator are a download away.
        const bool packed = core::languages::packFor(mine, languageCode()).has_value();
        if (!allOff && lookup && m_lookupIsWord && m_translator && !m_translator->hasOfflineDictionary(languageCode()) && packed)
            text += QLatin1Char('\n') + tr("Tip: the offline dictionary answers words without the internet. Download the "
                                           "%1 pack in Settings > General.").arg(core::languages::displayName(languageCode()));
        else if (!allOff && lookup && !m_lookupIsWord && packed && languageCode() != mine && m_offlineTranslator &&
                 !(m_offlineTranslator->hasProgram() && m_offlineTranslator->hasPair(languageCode(), mine)))
            text += QLatin1Char('\n') + tr("Tip: the offline translator translates sentences without the internet. Download "
                                           "the %1 pack in Settings > General.").arg(core::languages::displayName(languageCode()));
        if (m_lookupKind == core::TranslationRequest::Kind::Explain && !allOff)
            text = tr("Explanations come from Gemini or LM Studio, and neither could answer.") + QLatin1Char('\n') + text;
        m_translate->showNothing(text);
        if (m_lookupKind == core::TranslationRequest::Kind::Lookup) updateLookupHighlight();
    } else if (m_lookupKind == core::TranslationRequest::Kind::Explain) {
        // An explanation isn't vocabulary: shown, not logged.
        m_translate->showExplanation(r);
    } else {
        // A word: the Dictionary card (no "in this sentence": Explain This Sentence is for that). A passage: the
        // translation pop-up.
        if (m_lookupIsWord) showWordCard(r);
        else m_translate->showResult(r, m_dictionaryFirst);
        updateLookupHighlight();
        // Kept for a highlight of the same words right after (its meaning in the word log).
        m_lastLookupResult = r;
        // A lookup goes to the word log only when chosen (Practice > Add Looked-Up Words): by default the Word List
        // is what was highlighted (the owner's choice, 2026-11-26), with the place it was seen.
        if (m_words && m_bookId && m_settings.practiceLookups) {
            m_words->record(r, wordPlace(m_lookupSpine, m_lookupStart, m_lookupEnd, m_lookupSentence));
            emit wordLogged();
        }
    }
}

core::WordPlace ReaderView::wordPlace(int spine, int start, int end, const QString& sentence) const
{
    core::WordPlace place;
    place.bookId = m_bookId;
    place.bookTitle = m_title->text();
    place.language = languageCode();
    place.spine = spine;
    place.start = start;
    place.end = end;
    place.sentence = sentence;
    const core::Position at{spine, start};
    place.cfi = m_engine->cfiFor(at);
    if (const auto ix = m_engine->index()) place.pageLabel = ix->pageLabel(at);
    place.chapter = chapterTitleAt(at);
    return place;
}

int ReaderView::markHighlightedWord(const QString& highlighted, int spine, int start, int end, const QString& sentence)
{
    if (!m_words || !m_bookId || !m_settings.practiceHighlights) return 0;
    // Words and short phrases only (up to 4 words); a highlighted passage is a quote, not vocabulary.
    const QString text = core::vocabularyText(highlighted);
    if (text.isEmpty()) return 0;
    // The meaning of the lookup just shown when it was of these words (highlighted from its card, or right after);
    // else the offline dictionary's when it has one, else none yet.
    core::TranslationResult meaning;
    if (!m_lastLookupResult.isEmpty() && m_lastLookupResult.text.simplified().compare(text, Qt::CaseInsensitive) == 0)
        meaning = m_lastLookupResult;
    else if (m_translator)
        meaning = m_translator->offlineMeaning(text, languageCode());
    meaning.text = text;
    const int id = m_words->mark(meaning, wordPlace(spine, start, end, core::contextAround(sentence, text)));
    if (id) emit wordLogged();
    return id;
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
        const core::Annotation currentHl = current ? m_notes->get(current) : core::Annotation();
        const QString currentColor = currentHl.color;
        const bool currentHasNote = !currentHl.note.trimmed().isEmpty();
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(10, 6, 10, 6);
        h->setSpacing(10);
        // The most recently used color first: the easiest dot to reach (the owner's request, 2026-11-23).
        for (const QString& c : core::Annotations::colorsInOrder(m_settings.highlightOrder)) {
            auto* dot = new QToolButton;
            dot->setObjectName(QStringLiteral("Icon"));
            dot->setFixedSize(30, 30);
            dot->setCursor(Qt::PointingHandCursor);
            // The marked dot (the highlight's own color) takes the highlight away, unless it has a note.
            if (c == currentColor)
                dot->setToolTip(currentHasNote ? tr("This highlight has a note: Remove Highlight takes both away.") : tr("Remove Highlight"));
            else
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
        menu->addSeparator();
    }
    // Translate is always the first item (the owner's rule, 2026-11-23); Remove Highlight is with the note below.
    QAction* translate = menu->addAction(tr("Translate"), this, [this] { translateSelection(false); });
    translate->setProperty("testId", "menuTranslate");
    QAction* explain = menu->addAction(tr("Explain This Sentence"), this, [this] { explainSelection(); });
    explain->setProperty("testId", "menuExplain");
    explain->setToolTip(tr("The sentence in %1, with notes on its grammar and idioms (Gemini or LM Studio).")
                            .arg(core::languages::displayName(core::languages::readerLanguage())));
    QAction* dict = menu->addAction(tr("Dictionary"), this, [this] { showDictionary(); });
    dict->setProperty("testId", "menuDictionary");
    dict->setEnabled(words >= 1 && words <= 4);
    // Greyed out: why, on hover (the owner's request, 2026-11-26).
    if (!dict->isEnabled())
        dict->setToolTip(tr("The dictionary looks up 1 to 4 words: %1 are selected. Translate gives the meaning of a longer passage.")
                             .arg(QLocale().toString(words)));
    menu->setToolTipsVisible(true);
    // Text to speech: the selection read in the book's language (the owner's request, 2026-11-23).
    QAction* aloud = menu->addAction(tr("Read Aloud"), this, [this] { readSelectionAloud(); });
    aloud->setProperty("testId", "menuReadAloud");
    aloud->setToolTip(tr("The selected words, read in the book's language by the system's voice."));
    if (isReadingAloud()) {
        QAction* stop = menu->addAction(tr("Stop Reading Aloud"), this, [this] { stopReadingAloud(); });
        stop->setProperty("testId", "menuStopReadingAloud");
    }
    QAction* copy = menu->addAction(tr("Copy"), this, [this] {
        const QString t = m_pages->selectedText();
        if (!t.isEmpty()) QApplication::clipboard()->setText(t);
    });
    copy->setShortcut(QKeySequence::Copy);
    copy->setProperty("testId", "menuCopy");
    if (m_notes && m_bookId) {
        const int current = highlightAtSelection();
        const bool hasNote = current && !m_notes->get(current).note.isEmpty();
        if (current) {
            QAction* remove = menu->addAction(tr("Remove Highlight"), this, [this] { removeHighlightAtSelection(); });
            remove->setProperty("testId", "menuRemoveHighlight");
        }
        QAction* note = menu->addAction(hasNote ? tr("Edit Note…") : tr("Add Note…"), this, [this] {
            int id = highlightAtSelection();
            if (!id) id = highlightSelection(QStringLiteral("yellow"), false);
            if (id) editNote(id);
        });
        note->setProperty("testId", "menuNote");
        // A bookmark on the word itself (the header's button marks the page).
        const bool marked = wordBookmarkAtSelection() != 0;
        QAction* mark = menu->addAction(marked ? tr("Remove Bookmark Here") : tr("Bookmark Here"), this, [this] { toggleWordBookmark(); });
        mark->setProperty("testId", "menuBookmarkWord");
        mark->setToolTip(tr("A bookmark on this word, not the whole page"));
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

QList<QLocale> ReaderView::speechLocales()
{
    if (m_speechLocalesForTests) return *m_speechLocalesForTests;
#ifdef HAVE_TEXT_TO_SPEECH
    if (!m_interactive) return {};   // tests and screenshots never start the system's speech engine
    if (!m_speech) m_speech = new QTextToSpeech(this);
    if (m_speech->state() == QTextToSpeech::Error) return {};
    return m_speech->availableLocales();
#else
    return {};
#endif
}

QString ReaderView::speechProblem(const QString& language, bool* canOpenSettings)
{
    if (canOpenSettings) *canOpenSettings = false;
    const QLocale want(language.isEmpty() ? QStringLiteral("en") : language);
    const QList<QLocale> locales = speechLocales();
    for (const QLocale& l : locales)
        if (l.language() == want.language()) return {};
    if (!m_interactive && !m_speechLocalesForTests) return {};
    const QString name = core::languages::displayName(want.name().section(QLatin1Char('_'), 0, 0));
#ifdef Q_OS_WIN
    if (canOpenSettings) *canOpenSettings = true;
    // Another language's voice would read the word wrongly: nothing is said, and the fix is a free voice pack.
    return locales.isEmpty() ? tr("Windows has no voices for reading aloud. Add one (free) in Settings > Time & Language > Speech.")
                             : tr("No %1 voice is installed. Add one (free) in Windows Settings > Time & Language > Speech > Add voices.").arg(name);
#else
    return locales.isEmpty() ? tr("No speech engine was found. Install speech-dispatcher and espeak-ng (for example: sudo apt install "
                                  "speech-dispatcher espeak-ng), then start FreeReader again.")
                             : tr("No %1 voice is installed. Install one for speech-dispatcher, for example the espeak-ng package.").arg(name);
#endif
}

void ReaderView::readSelectionAloud()
{
    const QString text = m_pages->selectedText().simplified();
    if (text.isEmpty()) return;
    const QString problem = speechProblem(languageCode());
    if (!problem.isEmpty()) {
        m_pages->showNotice(problem);
        return;
    }
    m_lastReadAloud = text;
    speak(text, languageCode());
}

void ReaderView::stopReadingAloud()
{
#ifdef HAVE_TEXT_TO_SPEECH
    if (m_speech) m_speech->stop();
#endif
}

bool ReaderView::isReadingAloud() const
{
#ifdef HAVE_TEXT_TO_SPEECH
    return m_speech && m_speech->state() == QTextToSpeech::Speaking;
#else
    return false;
#endif
}

void ReaderView::speak(const QString& text, const QString& language)
{
    bool canOpenSettings = false;
    const QString problem = speechProblem(language, &canOpenSettings);
    if (!problem.isEmpty()) {
        m_translate->showSpeechProblem(problem, canOpenSettings);   // each shows it only while it is open
        m_dictCard->showSpeechProblem(problem, canOpenSettings);
        return;
    }
    // Settings > Audio: the speed and volume, and the voice chosen for this language.
    m_lastSpoken = SpokenWith{text, language, chosenVoice(language), m_settings.speechRatePct / 100.0, m_settings.speechVolumePct / 100.0};
#ifdef HAVE_TEXT_TO_SPEECH
    if (!m_interactive || text.isEmpty()) return;
    if (!m_speech) m_speech = new QTextToSpeech(this);
    const QLocale want(language.isEmpty() ? QStringLiteral("en") : language);
    bool voiceSet = false;
    if (!m_lastSpoken.voice.isEmpty()) {
        for (const QVoice& v : m_speech->findVoices())
            if (v.name() == m_lastSpoken.voice && v.locale().language() == want.language()) {
                m_speech->setLocale(v.locale());
                m_speech->setVoice(v);
                voiceSet = true;
                break;
            }
    }
    if (!voiceSet) {
        // The book's language, and its country when there is a voice for it (es-ES before es-MX for a book in es-ES).
        QLocale best;
        for (const QLocale& l : m_speech->availableLocales())
            if (l.language() == want.language() && (best.language() != want.language() || l.territory() == want.territory())) best = l;
        if (best.language() == want.language()) m_speech->setLocale(best);
    }
    m_speech->setRate(m_lastSpoken.rate);
    m_speech->setVolume(m_lastSpoken.volume);
    m_speech->say(text);
#else
    Q_UNUSED(text);
#endif
}

namespace {
QString languageKey(const QString& language)
{
    const QString l = language.isEmpty() ? QStringLiteral("en") : language;
    return QLocale::languageToCode(QLocale(l).language(), QLocale::ISO639Part1);
}
}  // namespace

QList<ReaderView::SpeechVoice> ReaderView::speechVoices()
{
    if (m_speechVoicesForTests) return *m_speechVoicesForTests;
#ifdef HAVE_TEXT_TO_SPEECH
    if (!m_interactive) return {};   // tests and screenshots never start the system's speech engine
    if (!m_speech) m_speech = new QTextToSpeech(this);
    if (m_speech->state() == QTextToSpeech::Error) return {};
    QList<SpeechVoice> out;
    for (const QVoice& v : m_speech->findVoices()) out << SpeechVoice{v.name(), v.locale()};
    return out;
#else
    return {};
#endif
}

QString ReaderView::chosenVoice(const QString& language) const
{
    const QString prefix = languageKey(language) + QLatin1Char('=');
    for (const QString& s : m_settings.speechVoices)
        if (s.startsWith(prefix)) return s.mid(prefix.size());
    return {};
}

QString ReaderView::playVoiceSample(const QString& language)
{
    const QString problem = speechProblem(language);
    if (!problem.isEmpty()) return problem;
    // A sentence in the language itself, so the voice is heard as it reads books.
    static const QHash<QString, QString> samples = {
        {QStringLiteral("en"), QStringLiteral("This is how this voice reads your books.")},
        {QStringLiteral("es"), QStringLiteral("Así lee esta voz tus libros.")},
        {QStringLiteral("fr"), QStringLiteral("Voici comment cette voix lit vos livres.")},
        {QStringLiteral("de"), QStringLiteral("So liest diese Stimme Ihre Bücher vor.")},
        {QStringLiteral("it"), QStringLiteral("Ecco come questa voce legge i tuoi libri.")},
        {QStringLiteral("pt"), QStringLiteral("É assim que esta voz lê os seus livros.")},
    };
    const QString key = languageKey(language);
    speak(samples.value(key, QLocale::languageToString(QLocale(key).language())), key);
    return {};
}

// ---------------------------------------------------------------- highlights, notes, bookmarks

int ReaderView::highlightAtSelection() const
{
    if (!m_notes || !m_bookId || !m_pages->hasSelection()) return 0;
    const auto hits = m_notes->highlightsAt(m_bookId, m_pages->selectionSpine(), m_pages->selectionStart(), m_pages->selectionEnd());
    return hits.isEmpty() ? 0 : hits.first().id;
}

int ReaderView::highlightSelection(const QString& color, bool chosen)
{
    if (!m_notes || !m_bookId || !m_pages->hasSelection() || m_pages->selectedText().trimmed().isEmpty()) return 0;
    const int spine = m_pages->selectionSpine(), start = m_pages->selectionStart(), end = m_pages->selectionEnd();
    const QString sentence = m_pages->selectionSentence();
    const core::HighlightChange c = core::highlightRange(*m_notes, *m_engine, m_bookId, spine, start, end, color,
                                                         chapterTitleAt({spine, start}), m_pages->selectedText());
    if (c.id && !c.recolored) markHighlightedWord(c.text, spine, c.from, c.to, sentence);
    // A color used (not one clicked again to take a highlight away) moves to the front of the dots.
    if (c.id && chosen) {
        m_settings.highlightOrder = core::Annotations::usedColor(m_settings.highlightOrder, color);
        emit appearanceChanged();   // the settings file needs saving
    }
    clearSelectionKeepingPopup();
    refreshAnnotations();
    return c.id;
}

void ReaderView::clearSelectionKeepingPopup()
{
    // A translation open for these words stays open while they're highlighted (the owner's request, 2026-11-22).
    m_keepPopup = true;
    m_pages->clearSelection();
    m_keepPopup = false;
}

ReaderView::KindleResult ReaderView::importKindle(const core::KindlePlan& plan, const KindleOptions& options)
{
    const core::Book* book = m_engine->book();
    if (!book || !m_bookId || !m_notes) return {};
    // Applied by the code shared with the phone (core/services/KindleApply.h).
    core::KindleTarget target;
    target.book = book;
    target.bookId = m_bookId;
    target.bookTitle = m_title->text();
    target.language = languageCode();
    target.index = m_engine->index();
    target.notes = m_notes;
    target.words = m_words;
    target.library = m_library;
    if (m_translator) target.offlineMeaning = [this, lang = target.language](const QString& w) { return m_translator->offlineMeaning(w, lang); };
    target.chapterTitle = [this](const core::Position& p) { return chapterTitleAt(p); };
    KindleResult res = core::applyKindlePlan(plan, options, target);
    if (options.moveToLastRead && plan.lastRead.spine >= 0) {
        m_pages->setPosition(plan.lastRead);
        savePosition();
        res.moved = true;
    }
    refreshAnnotations();
    if (res.words || res.missingWords) emit wordLogged();
    return res;
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
    if (!id) id = highlightSelection(QStringLiteral("yellow"), false);
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

int ReaderView::wordBookmarkAtSelection() const
{
    if (!m_notes || !m_bookId || !m_pages->hasSelection()) return 0;
    return m_notes->wordBookmarkAt(m_bookId, m_pages->selectionSpine(), m_pages->selectionStart(), m_pages->selectionEnd());
}

int ReaderView::toggleWordBookmark()
{
    if (!m_notes || !m_bookId || !m_pages->hasSelection() || !m_engine->isOpen()) return 0;
    if (const int existing = wordBookmarkAtSelection()) {
        m_notes->remove(existing);
        clearSelectionKeepingPopup();   // the word was selected only for the bookmark (the owner's request, 2026-11-23)
        refreshAnnotations();
        return 0;
    }
    const int spine = m_pages->selectionSpine();
    int from = 0, to = 0;
    core::firstWord(*m_engine, spine, m_pages->selectionStart(), m_pages->selectionEnd(), &from, &to);
    if (to <= from) return 0;
    core::Annotation b;
    b.bookId = m_bookId;
    b.kind = core::Annotation::Bookmark;
    b.spine = spine;
    b.start = from;
    b.end = to;
    // The word and what follows it, as the Notebook shows a bookmark.
    if (const auto cl = m_engine->layout(spine)) b.text = cl->flow->textRange(from, qMin(cl->flow->length, from + 120)).simplified();
    b.cfi = m_engine->cfiFor({spine, from});
    if (const auto ix = m_engine->index()) b.pageLabel = ix->pageLabel({spine, from});
    b.chapter = chapterTitleAt({spine, from});
    const int id = m_notes->add(b);
    clearSelectionKeepingPopup();
    refreshAnnotations();
    offerToRemoveEarlierBookmarks(id);
    return id;
}

QMessageBox* ReaderView::bookmarkQuestion(int earlier, const QString& where)
{
    auto* box = new QMessageBox(this);
    box->setWindowTitle(tr("Bookmark Added"));
    box->setIcon(QMessageBox::Question);
    box->setText(earlier == 1 ? tr("This book also has a bookmark on %1. Remove it?").arg(where)
                              : tr("This book also has %1 other bookmarks; the last one added is on %2. Remove it?").arg(QLocale().toString(earlier), where));
    QPushButton* removeLast = box->addButton(tr("Remove Previous Bookmark"), QMessageBox::AcceptRole);
    removeLast->setProperty("testId", "bookmarkRemovePrevious");
    if (earlier > 1) box->addButton(tr("Remove All %1").arg(QLocale().toString(earlier)), QMessageBox::DestructiveRole)->setProperty("testId", "bookmarkRemoveAll");
    QPushButton* keep = box->addButton(earlier == 1 ? tr("Keep Both") : tr("Keep All"), QMessageBox::RejectRole);
    keep->setProperty("testId", "bookmarkKeep");
    box->setDefaultButton(removeLast);
    box->setEscapeButton(keep);
    return box;
}

void ReaderView::applyPageTurnSettings()
{
    // From the settings (Settings > General > Page Turns, or Cancel there): the page and the Text and Layout panel.
    m_panel->setPageTurn(m_settings.pageTurn, m_settings.pageTurnMs, m_settings.pageTurnMotion);
    m_pages->setPageTurn(m_settings.pageTurn, m_settings.pageTurnMs, m_settings.pageTurnMotion);
    m_panel->setTurnArrows(m_settings.pageTurnArrows);
    m_pages->setTurnArrows(m_settings.pageTurnArrows);
}

void ReaderView::offerToRemoveEarlierBookmarks(int newId)
{
    // A new bookmark: remove the one before it? (the owner's request, 2026-11-26: a bookmark usually marks where
    // reading got to, and the old one is then in the way). The one added last before it, or all of them.
    if (!m_notes || !m_bookId || !newId) return;
    QList<core::Annotation> earlier;
    for (const core::Annotation& a : m_notes->forBook(m_bookId))
        if (a.kind == core::Annotation::Bookmark && a.id != newId) earlier << a;
    if (earlier.isEmpty()) return;
    std::sort(earlier.begin(), earlier.end(), [](const core::Annotation& a, const core::Annotation& b) {
        return a.created != b.created ? a.created > b.created : a.id > b.id;
    });
    const core::Annotation& last = earlier.first();
    const QString where = !last.pageLabel.isEmpty() ? tr("page %1").arg(last.pageLabel)
                                                    : QStringLiteral("“%1…”").arg(last.text.left(40).trimmed());
    const int n = int(earlier.size());
    BookmarkChoice choice = BookmarkChoice::Keep;
    if (m_bookmarkAnswerForTests) {
        choice = m_bookmarkAnswerForTests(n);
    } else if (m_interactive) {
        std::unique_ptr<QMessageBox> box(bookmarkQuestion(n, where));
        box->exec();
        const QString clicked = box->clickedButton() ? box->clickedButton()->property("testId").toString() : QString();
        if (clicked == QLatin1String("bookmarkRemovePrevious")) choice = BookmarkChoice::RemovePrevious;
        else if (clicked == QLatin1String("bookmarkRemoveAll")) choice = BookmarkChoice::RemoveAll;
    }
    if (choice == BookmarkChoice::Keep) return;
    if (choice == BookmarkChoice::RemovePrevious) m_notes->remove(last.id);
    else
        for (const core::Annotation& a : earlier) m_notes->remove(a.id);
    refreshAnnotations();
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
        const int id = m_notes->add(b);
        refreshAnnotations();
        offerToRemoveEarlierBookmarks(id);
        return true;
    }
    refreshAnnotations();
    return false;
}

void ReaderView::refreshAnnotations()
{
    const bool on = m_notes && m_bookId;
    const QList<core::Annotation> all = on ? m_notes->forBook(m_bookId) : QList<core::Annotation>();
    const int spine = m_pages->position().spine;
    std::vector<layout::Overlay> overlays;
    std::vector<std::pair<int, int>> marks;
    for (const core::Annotation& a : all)
        if (core::Annotations::isWordBookmark(a) && a.spine == spine) marks.push_back({a.start, a.end});
    m_pages->setWordBookmarks(spine, marks);
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
    // A pop-up open for highlighted (or no longer highlighted) words says so on its Highlight button.
    if (m_translate->isVisible() || m_dictCard->isVisible()) updateLookupHighlight();
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
    // A highlight or a word bookmark: its words are selected, so the place shows.
    if (a.kind == core::Annotation::Highlight || core::Annotations::isWordBookmark(a))
        m_pages->selectRange(p, {p.spine, p.offset + (a.end - a.start)});
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
    // The file name chooses the format: JSON, CSV, TSV, plain text, anything else Markdown.
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("json")) f.write(m_notes->json(m_bookId, title, b.author, b.path));
    else if (suffix == QLatin1String("csv")) f.write(QByteArray("\xEF\xBB\xBF") + m_notes->csv(m_bookId, title).toUtf8());   // a BOM: Excel reads it as UTF-8
    else if (suffix == QLatin1String("tsv")) f.write(m_notes->tsv(m_bookId, title).toUtf8());
    else if (suffix == QLatin1String("txt")) f.write(m_notes->plainText(m_bookId, title, b.author).toUtf8());
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
    // One panel on the right at a time.
    if (on && m_bilingual->isVisible()) setBilingualVisible(false);
    if (on && m_practicePanel->isVisible()) setPracticeVisible(false);
    if (on) emit rightPanelOpened();   // the window's Getting Started guide makes room
    if (on) makeRoom(true, 340);
    m_notebook->setVisible(on);
    const QSignalBlocker b(m_notebookButton);
    m_notebookButton->setChecked(on);
    m_settings.showNotebook = on;
    if (on) refreshAnnotations();
    emit appearanceChanged();   // the settings file needs saving
}

// ---------------------------------------------------------------- the bilingual view (English alongside)

bool ReaderView::bilingualVisible() const { return m_bilingual->isVisible(); }

void ReaderView::setBilingualVisible(bool on)
{
    if (on && m_notebook->isVisible()) setNotebookVisible(false);
    if (on && m_practicePanel->isVisible()) setPracticeVisible(false);
    if (on) emit rightPanelOpened();
    if (on) makeRoom(true, 380);
    m_bilingual->setVisible(on);
    if (m_settings.showBilingual != on) {
        m_settings.showBilingual = on;
        emit appearanceChanged();   // the settings file needs saving
    }
    if (on) refreshBilingual();
}

// ---------------------------------------------------------------- the practice workspace

void ReaderView::setPracticeView(PracticeView* view) { m_practicePanel->setView(view); }

bool ReaderView::practiceVisible() const { return m_practicePanel->isVisible(); }

void ReaderView::setPracticeVisible(bool on)
{
    PracticeView* v = m_practicePanel->view();
    if (!v) on = false;
    if (on && m_notebook->isVisible()) setNotebookVisible(false);
    if (on && m_bilingual->isVisible()) setBilingualVisible(false);
    if (on) emit rightPanelOpened();
    // Never narrower than its contents (a width saved before, or in another interface language).
    if (on && !m_practiceMaximized && m_practicePanel->width() < m_practicePanel->narrowest()) m_practicePanel->setFixedWidth(m_practicePanel->narrowest());
    if (on) makeRoom(true, m_practicePanel->width());
    const bool was = m_practicePanel->isVisible();
    if (!on && m_practiceMaximized) setPracticeMaximized(false);   // closed: the page comes back
    m_practicePanel->setVisible(on);
    {
        const QSignalBlocker b(m_practiceButton);
        m_practiceButton->setChecked(on);
    }
    if (on != was) {
        // The page laid out for its new width now, not after the pause a window's resize waits for (part of the
        // lag the owner saw on pressing Practice, 2026-11-26).
        if (layout()) layout()->activate();
        m_pages->flushResize();
    }
    if (on && !was) {
        updatePracticePage();
        // Words From and the Word List start on this book; a session already going carries on.
        if (v->page() == PracticeView::DonePage) v->showStart();
        else v->refresh();
    }
    if (m_settings.showPractice != on) {
        m_settings.showPractice = on;
        emit appearanceChanged();   // the settings file needs saving
    }
}

void ReaderView::setPracticeMaximized(bool on)
{
    if (on && !m_practicePanel->isVisible()) return;
    if (on == m_practiceMaximized) return;
    m_practiceMaximized = on;
    m_practicePanel->setMaximized(on);
    if (on) {
        // Over the whole page area: the page is hidden (it keeps its place), the panel takes the room.
        m_practicePanel->setMinimumWidth(0);
        m_practicePanel->setMaximumWidth(QWIDGETSIZE_MAX);
        m_practicePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_pages->hide();
    } else {
        m_practicePanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        m_practicePanel->setFixedWidth(qBound(m_practicePanel->narrowest(), m_settings.practiceWidth, 1600));
        m_pages->show();
        m_pages->setFocus();
    }
    // The Word List's book, lookups and next review, and Export, when there is room for them.
    if (PracticeView* v = m_practicePanel->view()) v->setWide(on);
}

void ReaderView::updatePracticePage()
{
    PracticeView* v = m_practicePanel->view();
    if (!v || !m_engine->isOpen()) return;
    const core::Position a = m_pages->screenStart(), e = m_pages->screenEnd();
    // The screen's end, when it is in a later spine item, counts as the end of this one.
    v->setPageRange(a.spine, a.offset, e.spine == a.spine ? e.offset : std::numeric_limits<int>::max());
}

void ReaderView::syncSpreadButton()
{
    // One page or two: as laid out now (Automatic picks by the room the pages have).
    const bool two = m_pages->columnCount() == 2;
    m_spreadButton->setChecked(two);
    m_spreadButton->setToolTip(two ? tr("One Page") : tr("Two Pages Side by Side"));
}

void ReaderView::toggleTwoPages()
{
    changeAppearance(QStringLiteral("columns"), m_pages->columnCount() == 2 ? QStringLiteral("one") : QStringLiteral("two"));
}

QList<ReaderView::ParagraphRef> ReaderView::visibleParagraphs() const
{
    QList<ParagraphRef> out;
    if (!m_engine->isOpen()) return out;
    const core::Position a = m_pages->screenStart(), e = m_pages->screenEnd();   // not position(): that can sit lower down the screen
    const auto cl = m_engine->layout(a.spine);
    if (!cl) return out;
    for (const layout::Block& b : cl->flow->blocks) {
        if (b.kind != layout::Block::Text) continue;
        if (b.start + b.length <= a.offset) continue;               // ended before the screen
        if (e.spine == a.spine && b.start >= e.offset) break;       // starts after it
        QString t = b.text;
        t.replace(QChar(0x2028), QLatin1Char(' '));
        t = t.simplified();
        int letters = 0;
        for (const QChar c : t)
            if (c.isLetter()) ++letters;
        if (letters < 2) continue;   // page numbers, ornaments, "* * *"
        out << ParagraphRef{a.spine, b.start, t};
    }
    return out;
}

QString ReaderView::paragraphKey(const QString& text) const
{
    const QByteArray raw = (QStringLiteral("paragraph|") + languageCode() + QLatin1Char('|') + core::languages::readerLanguage() +
                            QLatin1Char('|') + text).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
}

void ReaderView::refreshBilingual()
{
    m_bilingualTimer.stop();
    if (!m_bilingual->isVisible() || !m_engine->isOpen()) return;
    const QList<ParagraphRef> paras = visibleParagraphs();
    if (paras.isEmpty()) {
        m_bilingual->setMessage(tr("No text on this page."));
        return;
    }
    QStringList english, missing;
    for (const ParagraphRef& p : paras) {
        const QString t = m_library ? m_library->cachedTranslation(paragraphKey(p.text)).value(QLatin1String("translation")).toString() : QString();
        english << t;
        if (t.isEmpty() && !missing.contains(p.text)) missing << p.text;
    }
    if (missing.isEmpty()) {
        m_bilingual->setParagraphs(english);
        return;
    }
    if (!m_translator) {
        m_bilingual->setParagraphs(english, tr("Translation isn't available."));
        return;
    }
    // Already being translated (a page turned back and forth meanwhile): wait for it.
    for (const QStringList& asked : std::as_const(m_bilingualAsked))
        for (const QString& t : asked) missing.removeAll(t);
    if (!missing.isEmpty()) {
        // The page's paragraphs in one request (a very long page in a few).
        core::TranslationRequest r;
        r.kind = core::TranslationRequest::Kind::Paragraphs;
        r.from = languageCode();
        for (const QString& t : std::as_const(missing)) {
            r.paragraphs << (t.size() > 4000 ? t.left(4000) : t);
            if (r.paragraphs.size() == 25) break;
        }
        r.text = r.paragraphs.first().left(200);
        m_translator->setOfflineOnly(bookIsOffline());
        const int id = m_translator->translate(r);
        m_bilingualAsked.insert(id, r.paragraphs);
    }
    m_bilingual->setParagraphs(english, tr("Translating this page…"));
}

void ReaderView::onBilingualTranslated(int id, const core::TranslationResult& r, const QString& why)
{
    const QStringList asked = m_bilingualAsked.take(id);
    if (r.paragraphs.size() == asked.size() && !asked.isEmpty()) {
        if (r.keep) {
            for (int i = 0; i < asked.size(); ++i)
                if (m_library && !r.paragraphs[i].trimmed().isEmpty())
                    m_library->cacheTranslation(paragraphKey(asked[i]), r.provider,
                                                QJsonObject{{QStringLiteral("translation"), r.paragraphs[i].trimmed()}, {QStringLiteral("provider"), r.provider}});
            refreshBilingual();
        } else {
            // The offline translator's English: shown for this page, not kept (Gemini's replaces it when online).
            QStringList english;
            for (const ParagraphRef& p : visibleParagraphs()) {
                const int at = int(asked.indexOf(p.text));
                const QString saved = m_library ? m_library->cachedTranslation(paragraphKey(p.text)).value(QLatin1String("translation")).toString() : QString();
                english << (!saved.isEmpty() ? saved : at >= 0 ? r.paragraphs[at].trimmed() : QString());
            }
            m_bilingual->setParagraphs(english, geminiForExplainOnly()
                                                    ? tr("Translated offline (Bergamot).")
                                                    : tr("Translated offline (Bergamot); the online translation replaces it when Gemini or LM Studio can answer."));
        }
        return;
    }
    if (!m_bilingual->isVisible()) return;
    // Nothing could translate the page: keep what is there and say why.
    const bool allOff = !m_settings.useGemini && !m_settings.useLmStudio && !m_settings.useOfflineTranslator;
    const QString mine = core::languages::displayName(core::languages::readerLanguage());
    QString text = allOff ? tr("Turn on Gemini, LM Studio or the offline translator (Settings > Translation) to see the %1 of whole paragraphs.").arg(mine)
                          : tr("The %1 for this page couldn't be fetched.").arg(mine) + (why.isEmpty() ? QString() : QLatin1Char('\n') + why);
    if (bookIsOffline() && m_settings.useGemini) text += QLatin1Char('\n') + tr("This book is kept offline, so Gemini isn't asked.");
    else if (geminiForExplainOnly()) text += QLatin1Char('\n') + tr("Gemini is used only for Explain This Sentence (Settings > Translation).");
    m_bilingual->setParagraphs(m_bilingual->paragraphs(), text);
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
    makeRoom(false, 320);
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

void ReaderView::stepTextSize(int delta)
{
    const int now = effectiveAppearance().textSizePct;
    const int v = delta == 0 ? 100 : qBound(60, now + delta, 300);
    changeAppearance(QStringLiteral("text_size_pct"), v);
    m_pages->showNotice(tr("Text Size: %1 px").arg(qRound(20.0 * v / 100.0)));
}

void ReaderView::stepMargins(int delta)
{
    const int now = effectiveAppearance().marginPx;
    const int v = delta == 0 ? Appearance().marginPx : qBound(8, now + delta, 200);
    changeAppearance(QStringLiteral("margin_px"), v);
    m_pages->showNotice(tr("Margins: %1 px").arg(v));
}

void ReaderView::showAppearancePanel()
{
    if (!m_panel->isVisible()) m_aaButton->click();
}

#include "MainWindow.h"

#include "AppData.h"
#include "Popups.h"
#include "ReaderView.h"
#include "LibraryView.h"
#include "PracticeView.h"
#include "StartupTrace.h"
#include "LibrarySearchView.h"
#include "StatisticsView.h"

#include <QLineEdit>
#include "SettingsDialog.h"
#include "KindleImportDialog.h"
#include "core/services/KindleImport.h"
#include "ThemePicker.h"

#include "core/format/FormatPlugin.h"
#include "core/services/BookEngine.h"
#include "core/services/Importer.h"
#include "core/services/Library.h"
#include "core/services/Secrets.h"
#include "core/services/WordLog.h"
#include "core/services/Annotations.h"
#include "core/translate/AiProviders.h"
#include "core/translate/TranslationService.h"
#include "core/translate/OfflineDictionary.h"
#include "core/translate/OfflineTranslator.h"
#include "core/translate/SavedWords.h"

#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QHBoxLayout>
#include <QGraphicsDropShadowEffect>
#include "HelpPanel.h"
#include "WelcomeDialog.h"
#include "InterfaceLanguage.h"
#include "core/translate/LanguagePacks.h"

#include <utility>
#include <QMessageBox>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QScreen>
#include <QStackedWidget>
#include <QStyleHints>
#include <QTimer>

MainWindow::MainWindow(Settings& settings, core::Library* library, bool interactive, QWidget* parent)
    : QMainWindow(parent), m_settings(settings), m_library(library), m_interactive(interactive)
{
    setWindowTitle(QStringLiteral(APP_NAME));
    setAcceptDrops(true);
    core::languages::setReaderLanguage(m_settings.readerLanguage);   // the engines answer in it
    m_builtLanguage = interfaceLanguage();
    // The first start (and once for anyone who updated): the welcome, with the language and its packs. Never in
    // the tests or a screenshot run.
    if (interactive && !m_settings.welcomeShown) QTimer::singleShot(600, this, &MainWindow::showWelcome);
    m_engine = new core::BookEngine(this);
    // Translation: Gemini first, then LM Studio (the offline ones join the list when they are installed).
    m_net = new QNetworkAccessManager(this);
    m_gemini = new core::GeminiProvider(m_net, this);
    m_lmStudio = new core::LmStudioProvider(m_net, this);
    m_translator = new core::TranslationService(this);
    m_translator->setCache(m_library);
    // The offline dictionaries, when installed: Spanish -> English, and English definitions for English books.
    m_dictionary = new core::OfflineDictionary(this);
    m_dictionary->open(dictionaryFile(QStringLiteral("es")));
    m_dictionaryEn = new core::OfflineDictionary(this);
    m_dictionaryEn->open(dictionaryFile(QStringLiteral("en")));
    // Spanish definitions (Spanish -> Spanish), for the Dictionary card only: never a translation.
    m_definitionsEs = new core::OfflineDictionary(this);
    m_definitionsEs->open(dictionaryFile(QStringLiteral("es-es")));
    // English -> Spanish, for readers whose language is Spanish.
    m_dictionaryEnEs = new core::OfflineDictionary(this);
    m_dictionaryEnEs->open(dictionaryFile(QStringLiteral("en-es")));
    // When Gemini and LM Studio can't answer: the meaning saved in the Word List from an earlier lookup, the
    // offline dictionaries (words), then the offline translator (sentences and passages). Each dictionary answers
    // only in its own language pair, so the reader's language picks them (a Spanish reader: English -> Spanish, and
    // Spanish definitions for Spanish books).
    m_savedWords = new core::SavedWords(this);
    m_savedWords->setDictionaries({m_dictionary, m_dictionaryEn, m_dictionaryEnEs, m_definitionsEs});
    m_offline = new core::OfflineTranslator(this);
    m_offline->setFolder(translatorFolder());
    m_translator->setProviders({m_gemini, m_lmStudio, m_savedWords, m_dictionary, m_dictionaryEn, m_dictionaryEnEs, m_definitionsEs, m_offline});
    m_translator->setInstant({m_dictionary, m_dictionaryEn, m_dictionaryEnEs, m_definitionsEs});
    m_gemini->setKey(core::secrets::read(QStringLiteral("gemini-api-key")));
    m_stack = new QStackedWidget;
    m_start = new LibraryView(m_library, m_settings);
    m_reader = new ReaderView(m_engine, m_library, m_settings);
    m_reader->setInteractive(interactive);
    m_reader->setTranslator(m_translator);
    m_words = std::make_unique<core::WordLog>(m_library);
    m_reader->setWordLog(m_words.get());
    m_reader->setOfflineTranslator(m_offline);
    m_reader->setDictionaries({m_definitionsEs, m_dictionaryEn, m_dictionary, m_dictionaryEnEs});
    m_savedWords->setWordLog(m_words.get());
    m_annotations = std::make_unique<core::Annotations>(m_library);
    m_reader->setAnnotations(m_annotations.get());
    m_practice = new PracticeView(m_words.get(), m_settings);
    m_practice->setInteractive(interactive);
    m_practice->setTranslator(m_translator);
    m_stack->addWidget(m_practice);
    // The practice workspace beside the page while reading: its own view of the same word log.
    m_practiceSide = new PracticeView(m_words.get(), m_settings);
    m_practiceSide->setInteractive(interactive);
    m_practiceSide->setTranslator(m_translator);
    m_reader->setPracticeView(m_practiceSide);
    m_start->setPracticeCount([this] { return m_words->count(); });
    m_stack->addWidget(m_start);
    m_stack->addWidget(m_reader);
    m_librarySearch = new LibrarySearchView(m_library);
    m_stack->addWidget(m_librarySearch);
    m_statistics = new StatisticsView(m_library);
    m_stack->addWidget(m_statistics);
    m_stack->setCurrentWidget(m_start);   // the library first, whatever order the screens were added in
    // The screens, and the Getting Started guide beside them on the right when it is open.
    m_body = new QWidget;
    m_bodyLay = new QHBoxLayout(m_body);
    m_bodyLay->setContentsMargins(0, 0, 0, 0);
    m_bodyLay->setSpacing(0);
    m_bodyLay->addWidget(m_stack, 1);
    m_help = new HelpPanel;
    m_help->setMinimumWidth(0);
    m_help->setMaximumWidth(420);
    m_help->hide();
    m_bodyLay->addWidget(m_help);
    setCentralWidget(m_body);   // its size decides where the guide goes (placeHelp, from eventFilter: the app's filter)
    connect(m_help, &HelpPanel::closeRequested, this, [this] { setHelpVisible(false); });
    connect(m_stack, &QStackedWidget::currentChanged, this, [this] {
        if (helpVisible()) QTimer::singleShot(0, this, &MainWindow::placeHelp);   // below the new screen's top bar
    });
    connect(m_reader, &ReaderView::rightPanelOpened, this, [this] {
        if (helpVisible()) setHelpVisible(false);
    });
    connect(m_help, &HelpPanel::commandRequested, this, [this](const QString& id) { runCommand(id); });
    m_palette = new CommandPalette(m_stack);
    m_imageViewer = new ImageViewer(m_stack);
    // The theme list belongs to the window, not to its body: previews restyle the body only, never the list.
    m_themePicker = new ThemePicker(this);
    connect(m_themePicker, &ThemePicker::previewRequested, this, &MainWindow::previewTheme);
    connect(m_themePicker, &ThemePicker::chosen, this, &MainWindow::chooseTheme);
    connect(m_reader, &ReaderView::themeChosen, this, &MainWindow::chooseTheme);
    connect(m_reader, &ReaderView::appearanceChanged, this, &MainWindow::saveSettingsSoon);
    connect(m_reader, &ReaderView::moreThemesRequested, this, [this](const QPoint& p) { m_themePicker->showFor(m_settings.theme, p); });
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_settings.theme == QLatin1String("system")) showTheme(Theme::forChoice(QStringLiteral("system")), false);
    });

    m_settingsTimer = new QTimer(this);
    m_settingsTimer->setSingleShot(true);
    m_settingsTimer->setInterval(400);
    connect(m_settingsTimer, &QTimer::timeout, this, &MainWindow::saveSettingsNow);

    connect(m_engine, &core::BookEngine::opened, this, &MainWindow::onOpened);
    connect(m_engine, &core::BookEngine::openFailed, this, &MainWindow::onOpenFailed);
    connect(m_start, &LibraryView::openRequested, this, &MainWindow::openBook);
    connect(m_start, &LibraryView::settingsLinkClicked, this, &MainWindow::changeDataFolder);
    connect(m_start, &LibraryView::settingsChanged, this, &MainWindow::saveSettingsSoon);
    // The library's folders: books put there since the last start are added (quietly when there are none).
    if (interactive) QTimer::singleShot(400, m_start, &LibraryView::checkFolders);
    m_start->setPrompt([this](const QString& title, const QString& placeholder, std::function<QString(const QString&)> accept) {
        m_palette->showPrompt(title, placeholder, std::move(accept));
    });
    m_start->setConfirm([this](const QString& title, const QString& text) {
        // Tests and the screenshot mode never show a dialog.
        return !m_interactive || QMessageBox::question(this, title, text) == QMessageBox::Yes;
    });
    connect(m_reader, &ReaderView::settingsLinkClicked, this, &MainWindow::changeDataFolder);
    connect(m_reader, &ReaderView::libraryRequested, this, &MainWindow::showLibrary);
    connect(m_reader, &ReaderView::fullScreenRequested, this, &MainWindow::toggleFullScreen);
    connect(m_reader, &ReaderView::translationSettingsRequested, this, [this] { showSettings(SettingsDialog::TranslationTab); });
    connect(m_start, &LibraryView::practiceRequested, this, [this] { showPractice(); });
    connect(m_start, &LibraryView::librarySearchRequested, this, [this](const QString& q) { showLibrarySearch(q); });
    connect(m_start, &LibraryView::latestBookmarkRequested, this, &MainWindow::goToLatestBookmark);
    connect(m_reader, &ReaderView::latestBookmarkRequested, this, &MainWindow::goToLatestBookmark);
    connect(m_librarySearch, &LibrarySearchView::backRequested, this, [this] {
        QWidget* to = m_beforeLibrarySearch == m_reader && m_engine->isOpen() ? static_cast<QWidget*>(m_reader) : static_cast<QWidget*>(m_start);
        m_stack->setCurrentWidget(to);
        if (to == m_start) m_start->refresh();
        else m_reader->pages()->setFocus();
    });
    connect(m_statistics, &StatisticsView::backRequested, this, [this] {
        QWidget* to = m_beforeStatistics == m_reader && m_engine->isOpen() ? static_cast<QWidget*>(m_reader) : static_cast<QWidget*>(m_start);
        m_stack->setCurrentWidget(to);
        if (to == m_start) m_start->refresh();
        else m_reader->pages()->setFocus();
    });
    connect(m_statistics, &StatisticsView::openBookRequested, this, [this](int bookId) {
        const core::LibraryBook b = m_library ? m_library->book(bookId) : core::LibraryBook();
        if (b.id && QFileInfo::exists(b.path)) openBook(b.path);
    });
    connect(m_librarySearch, &LibrarySearchView::openRequested, this, [this](int bookId, const QString& path, int spine, int start, int end) {
        m_pendingJump = {QString(), spine, start, end};
        if (m_engine->isOpen() && m_reader->bookId() == bookId) {
            m_stack->setCurrentWidget(m_reader);
            applyPendingJump();
        } else {
            openBook(path);
        }
    });
    connect(m_practice, &PracticeView::settingsChanged, this, &MainWindow::saveSettingsSoon);
    connect(m_practiceSide, &PracticeView::settingsChanged, this, &MainWindow::saveSettingsSoon);
    // Show in Book from the workspace: the same book jumps there, beside the workspace.
    connect(m_practiceSide, &PracticeView::showInBookRequested, m_practice, &PracticeView::showInBookRequested);
    // Show Beside the Book (the full window's button): back to the book, Practice beside it on the same tab.
    connect(m_practice, &PracticeView::besideBookRequested, this, [this] {
        if (!m_engine->isOpen()) return;
        const bool list = m_practice->page() == PracticeView::ListPage;
        m_stack->setCurrentWidget(m_reader);
        m_reader->setPracticeVisible(true);
        if (list) m_practiceSide->showList();
        m_reader->pages()->setFocus();
    });
    connect(m_practice, &PracticeView::backRequested, this, [this] {
        QWidget* to = m_beforePractice == m_reader && m_engine->isOpen() ? static_cast<QWidget*>(m_reader) : static_cast<QWidget*>(m_start);
        m_stack->setCurrentWidget(to);
        if (to == m_start) m_start->refresh();
        else m_reader->pages()->setFocus();
    });
    connect(m_practice, &PracticeView::showInBookRequested, this, [this](int bookId, const QString& cfi, int spine, int start, int end) {
        const core::LibraryBook b = m_library ? m_library->book(bookId) : core::LibraryBook();
        if (b.id == 0 || !QFileInfo::exists(b.path)) {
            m_start->setMessage(tr("That book's file wasn't found."));
            m_stack->setCurrentWidget(m_start);
            return;
        }
        m_pendingJump = {cfi, spine, start, end};
        m_reader->setPracticeMaximized(false);   // the page, to see the word on
        if (m_engine->isOpen() && m_reader->bookId() == bookId) {
            m_stack->setCurrentWidget(m_reader);
            applyPendingJump();
        } else {
            openBook(b.path);
        }
    });
    connect(m_reader, &ReaderView::imageRequested, this, [this](const QString& path) {
        QImage img;
        img.loadFromData(m_engine->resource(path));
        m_imageViewer->showImage(img);
    });

    m_start->setSettingsNotKept(appdata::isTemporary());
    m_reader->setSettingsNotKept(appdata::isTemporary());

    applyTranslationSettings();
    buildCommands();
    buildMenus();
    syncChecks();
    showTheme(Theme::forChoice(m_settings.theme), false);

    if (!m_settings.geometry.isEmpty()) {
        restoreGeometry(m_settings.geometry);
    } else {
        const QRect avail = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 800);
        resize(qMin(1280, avail.width() - 80), qMin(860, avail.height() - 80));
    }
}

MainWindow::~MainWindow() = default;

bool MainWindow::eventFilter(QObject* o, QEvent* e)
{
    // The window's body changed size: the guide beside the screen or over it (placeHelp).
    if (o == m_body && e->type() == QEvent::Resize && m_help && m_help->isVisible()) {
        QTimer::singleShot(0, this, &MainWindow::placeHelp);
        return false;
    }
    // Every menu shows its items' tooltips (Qt's menus hide them unless asked): above all why an item is greyed out
    // (the owner's request, 2026-11-26).
    if (e->type() == QEvent::Polish)
        if (auto* m = qobject_cast<QMenu*>(o)) m->setToolTipsVisible(true);
    // A mouse button set as a shortcut (Settings > Keyboard Shortcuts > Mouse), pressed anywhere in this window.
    if (e->type() != QEvent::MouseButtonPress && e->type() != QEvent::MouseButtonDblClick) return false;
    auto* w = qobject_cast<QWidget*>(o);
    if (!w || w->window() != this) return false;
    auto* me = static_cast<QMouseEvent*>(e);
    const QString binding = mouse_shortcut::fromEvent(me->button(), me->modifiers());
    if (binding.isEmpty()) return false;
    for (auto& c : m_commands)
        if (c->mouse == binding) {
            if (c->isEnabled()) c->run();
            return true;
        }
    return false;
}

void MainWindow::addCommand(const QString& id, const QString& label, const QKeySequence& key, std::function<void()> run,
                            std::function<bool()> enabled, const QString& hint)
{
    auto c = std::make_unique<Command>();
    c->id = id;
    c->label = label;
    c->hint = hint;
    c->shortcut = key;
    c->defaultShortcut = key;

    c->run = std::move(run);
    c->enabled = std::move(enabled);
    c->action = new QAction(label, this);
    if (!c->shortcut.isEmpty()) c->action->setShortcut(c->shortcut);
    c->action->setShortcutContext(Qt::WindowShortcut);
    Command* raw = c.get();
    connect(c->action, &QAction::triggered, this, [raw] {
        if (raw->isEnabled() && raw->run) raw->run();
    });
    addAction(c->action);
    m_commands.push_back(std::move(c));
}

void MainWindow::buildCommands()
{
    auto reading = [this] { return isReading(); };
    addCommand(QStringLiteral("commands"), tr("Show Commands"), QKeySequence(Qt::CTRL | Qt::Key_K), [this] {
        std::vector<Command*> list;
        for (auto& c : m_commands)
            if (c->id != QLatin1String("commands")) list.push_back(c.get());
        m_palette->setCommands(list);
        m_palette->showCommands();
    });
    addCommand(QStringLiteral("open"), tr("Open Book…"), QKeySequence::Open, [this] { chooseBook(); });
    addCommand(QStringLiteral("library"), tr("Go to Library"), QKeySequence(Qt::CTRL | Qt::Key_L), [this] { showLibrary(); }, reading);
    auto inLibrary = [this] { return m_stack->currentWidget() == m_start; };
    addCommand(QStringLiteral("add-books"), tr("Add Books…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), [this] {
        showLibrary();
        if (m_interactive) m_start->chooseFilesToAdd();
    }, nullptr, tr("Add EPUB files to the library. They stay where they are."));
    addCommand(QStringLiteral("add-folder"), tr("Add Folder…"), QKeySequence(), [this] {
        showLibrary();
        if (m_interactive) m_start->chooseFolderToAdd();
    }, nullptr, tr("Add every EPUB file in a folder and its subfolders."));
    addCommand(QStringLiteral("new-collection"), tr("New Collection…"), QKeySequence(), [this] { m_start->newCollection(); }, inLibrary);
    addCommand(QStringLiteral("library-grid"), tr("Show Covers"), QKeySequence(), [this] { m_start->setListMode(false); saveSettingsSoon(); }, inLibrary);
    addCommand(QStringLiteral("library-list"), tr("Show List"), QKeySequence(), [this] { m_start->setListMode(true); saveSettingsSoon(); }, inLibrary);
    addCommand(QStringLiteral("contents"), tr("Show or Hide Contents"), QKeySequence(Qt::CTRL | Qt::Key_T),
               [this] { m_reader->setContentsVisible(!m_reader->contentsVisible()); saveSettingsSoon(); }, reading);
    addCommand(QStringLiteral("goto"), tr("Go to Page or Percent…"), QKeySequence(Qt::CTRL | Qt::Key_G), [this] {
        m_palette->showPrompt(tr("Go to Page or Percent"), tr("Page number, or a percentage such as 45%"),
                              [this](const QString& t) { return m_reader->goToPageOrPercent(t); });
    }, reading);
    addCommand(QStringLiteral("back"), tr("Back"), QKeySequence(Qt::ALT | Qt::Key_Left), [this] { m_reader->goBack(); },
               [this] { return isReading() && m_reader->canGoBack(); }, tr("Return to where you were before the last jump."));
    // No keys of their own (the arrow keys turn pages already): for a mouse button, or a key of one's choosing.
    addCommand(QStringLiteral("next-page"), tr("Next Page"), QKeySequence(), [this] { m_reader->pages()->nextScreen(); }, reading);
    addCommand(QStringLiteral("previous-page"), tr("Previous Page"), QKeySequence(), [this] { m_reader->pages()->previousScreen(); }, reading);
    addCommand(QStringLiteral("next-chapter"), tr("Next Chapter"), QKeySequence(Qt::CTRL | Qt::Key_PageDown), [this] { m_reader->nextChapter(); }, reading);
    addCommand(QStringLiteral("previous-chapter"), tr("Previous Chapter"), QKeySequence(Qt::CTRL | Qt::Key_PageUp),
               [this] { m_reader->previousChapter(); }, reading);
    addCommand(QStringLiteral("book-start"), tr("Go to Beginning of Book"), QKeySequence(Qt::CTRL | Qt::Key_Home),
               [this] { m_reader->jumpTo({0, 0}); }, reading);
    addCommand(QStringLiteral("copy"), tr("Copy"), QKeySequence::Copy, [this] {
        const QString t = m_reader->pages()->selectedText();
        if (!t.isEmpty()) QApplication::clipboard()->setText(t);
    }, [this] { return isReading() && m_reader->pages()->hasSelection(); });
    addCommand(QStringLiteral("larger"), tr("Larger Text"), QKeySequence(Qt::CTRL | Qt::Key_Equal), [this] { changeTextSize(10); }, nullptr,
               tr("While reading: larger text (also Ctrl+wheel over the page). In the library: larger covers."));
    addCommand(QStringLiteral("smaller"), tr("Smaller Text"), QKeySequence(Qt::CTRL | Qt::Key_Minus), [this] { changeTextSize(-10); }, nullptr,
               tr("While reading: smaller text. In the library: smaller covers."));
    addCommand(QStringLiteral("default-size"), tr("Default Text Size"), QKeySequence(Qt::CTRL | Qt::Key_0), [this] { changeTextSize(0); }, nullptr,
               tr("While reading: the default text size. In the library: the default cover size."));
    addCommand(QStringLiteral("wider-margins"), tr("Wider Margins"), QKeySequence(Qt::CTRL | Qt::Key_BracketRight), [this] { m_reader->stepMargins(16); },
               reading, tr("More space left and right of the text (also Ctrl+Shift+wheel over the page)."));
    addCommand(QStringLiteral("narrower-margins"), tr("Narrower Margins"), QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), [this] { m_reader->stepMargins(-16); },
               reading, tr("Less space left and right of the text."));
    addCommand(QStringLiteral("default-margins"), tr("Default Margins"), QKeySequence(), [this] { m_reader->stepMargins(0); }, reading);
    addCommand(QStringLiteral("columns-one"), tr("One Column"), QKeySequence(), [this] { setColumns(QStringLiteral("one")); });
    addCommand(QStringLiteral("columns-two"), tr("Two Columns"), QKeySequence(), [this] { setColumns(QStringLiteral("two")); });
    addCommand(QStringLiteral("columns-auto"), tr("Automatic Columns"), QKeySequence(), [this] { setColumns(QStringLiteral("auto")); },
               nullptr, tr("Two columns when the window is wide enough."));
    addCommand(QStringLiteral("appearance"), tr("Text and Layout…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), [this] { m_reader->showAppearancePanel(); },
               reading, tr("Font, text size, spacing, margins, page width, columns."));
    addCommand(QStringLiteral("themes"), tr("Choose Theme…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), [this] {
        m_themePicker->showFor(m_settings.theme, QPoint(width() - 8, 56));
    });
    addCommand(QStringLiteral("fullscreen"), tr("Full Screen"), QKeySequence(Qt::Key_F11), [this] { toggleFullScreen(); });
    addCommand(QStringLiteral("data-folder"), tr("Change App Data Folder…"), QKeySequence(), [this] { changeDataFolder(); });
    addCommand(QStringLiteral("about"), tr("About FreeReader"), QKeySequence(), [this] { about(); });
    addCommand(QStringLiteral("quit"), tr("Quit"), QKeySequence(Qt::CTRL | Qt::Key_Q), [this] { close(); });
    // The Settings menu opens the Settings window at each of its tabs (no "Settings…" inside Settings).
    addCommand(QStringLiteral("settings"), tr("General…"), QKeySequence(Qt::CTRL | Qt::Key_Comma),
               [this] { showSettings(SettingsDialog::GeneralTab); }, nullptr,
               tr("The app data folder, the library's folders, and resetting text and layout."));
    addCommand(QStringLiteral("audio-settings"), tr("Audio…"), QKeySequence(), [this] { showSettings(SettingsDialog::AudioTab); },
               nullptr, tr("The speed, volume and voice for Hear Pronunciation and Read Aloud."));
    addCommand(QStringLiteral("translate"), tr("Translate Selection"), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { m_reader->translateSelection(); },
               [this] { return isReading() && m_reader->pages()->hasSelection(); }, tr("Shows the translation pop-up for the selected text."));
    addCommand(QStringLiteral("explain"), tr("Explain This Sentence"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), [this] { m_reader->explainSelection(); },
               [this] { return isReading() && m_reader->pages()->hasSelection(); },
               tr("The selected sentence in %1, with notes on its grammar and idioms.").arg(core::languages::displayName(core::languages::readerLanguage())));
    addCommand(QStringLiteral("book-offline"), tr("Keep This Book Offline"), QKeySequence(), [this] {
        m_reader->setBookOffline(!m_reader->bookIsOffline());
        if (m_settingsDialog) m_settingsDialog->refreshOfflineBooks();
    }, [this] { return isReading() && m_settings.useGemini; },
       tr("Text from this book is never sent to Gemini; the engines on this computer still answer."));
    if (Command* c = command(QStringLiteral("book-offline"))) c->action->setCheckable(true);
    addCommand(QStringLiteral("pages-right-to-left"), tr("Pages Right to Left"), QKeySequence(), [this] {
        m_reader->setPagesRightToLeft(!m_reader->pagesRightToLeft());
        syncChecks();
        saveSettingsSoon();
    }, reading, tr("For manga: the left edge and the Left key turn to the next page."));
    if (Command* c = command(QStringLiteral("pages-right-to-left"))) c->action->setCheckable(true);
    addCommand(QStringLiteral("import-kindle"), tr("Import Highlights…"), QKeySequence(), [this] { importKindle(); }, reading,
               tr("Highlights, notes and bookmarks into this book: from a Kindle (Bookcision's JSON or My Clippings.txt), or "
                  "from a Notebook export (Markdown, plain text, CSV, TSV or JSON)."));
    addCommand(QStringLiteral("search-book"), tr("Search This Book…"), QKeySequence(Qt::CTRL | Qt::Key_F), [this] {
        m_reader->openSearch(m_reader->pages()->hasSelection() ? m_reader->pages()->selectedText() : QString());
    }, reading, tr("Every place a word or phrase appears, with its page."));
    addCommand(QStringLiteral("search-library"), tr("Search All Books…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), [this] {
        showLibrarySearch(isReading() && m_reader->pages()->hasSelection() ? m_reader->pages()->selectedText() : QString());
    }, nullptr, tr("A word or phrase in the text of every book in the library."));
    addCommand(QStringLiteral("read-aloud"), tr("Read Aloud"), QKeySequence(), [this] {
        if (m_reader->isReadingAloud()) m_reader->stopReadingAloud();
        else m_reader->readSelectionAloud();
    }, reading, tr("Reads the selected words aloud in the book's language; again stops it."));
    addCommand(QStringLiteral("bookmark"), tr("Bookmark This Page"), QKeySequence(Qt::CTRL | Qt::Key_D), [this] { m_reader->toggleBookmark(); },
               reading, tr("Adds a bookmark, or removes the one on this page."));
    // The bookmark added last, in any book, from anywhere (the owner's request, 2026-11-25).
    addCommand(QStringLiteral("latest-bookmark"), tr("Go to Latest Bookmark"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D),
               [this] { goToLatestBookmark(); }, nullptr, tr("Opens the book with the bookmark you added last, at its page."));
    addCommand(QStringLiteral("notebook"), tr("Show or Hide Notebook"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
               [this] { m_reader->setNotebookVisible(!m_reader->notebookVisible()); saveSettingsSoon(); }, reading,
               tr("The book's highlights, notes and bookmarks."));
    // While reading: the practice workspace beside the page, on or off; elsewhere: Practice in the whole window.
    addCommand(QStringLiteral("practice"), tr("Practice Words"), QKeySequence(Qt::CTRL | Qt::Key_P), [this] {
        if (isReading()) m_reader->setPracticeVisible(!m_reader->practiceVisible());
        else showPractice();
    }, nullptr,
               tr("Flashcards of the words you looked up. While reading, they open beside the page."));
    // Named for the reader's language ("Show English Alongside"; for a Spanish reader, Spanish).
    const QString mine = core::languages::displayName(core::languages::readerLanguage());
    addCommand(QStringLiteral("bilingual"), tr("Show %1 Alongside").arg(mine), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B), [this] {
        m_reader->setBilingualVisible(!m_reader->bilingualVisible());
        syncChecks();
        saveSettingsSoon();
    }, [this] { return isReading(); }, tr("The %1 of every paragraph on the page, in a panel beside it (Gemini or LM Studio).").arg(mine));
    command(QStringLiteral("bilingual"))->action->setCheckable(true);
    addCommand(QStringLiteral("statistics"), tr("Statistics"), QKeySequence(), [this] { showStatistics(); }, nullptr,
               tr("Time read, days in a row, books finished, words and reviews."));
    addCommand(QStringLiteral("word-list"), tr("Word List"), QKeySequence(), [this] { showPractice(true); }, nullptr,
               tr("Every word you looked up, with its meaning."));
    addCommand(QStringLiteral("translation-settings"), tr("All Translation Options…"), QKeySequence(),
               [this] { showSettings(SettingsDialog::TranslationTab); }, nullptr, tr("Gemini key, LM Studio model, offline dictionary."));
    // The translation switches, also in Settings > Translation: each engine on its own, and offline only.
    auto toggle = [this](const char* id, const QString& label, bool Settings::*field, const QString& hint) {
        addCommand(QString::fromLatin1(id), label, QKeySequence(), [this, field] {
            m_settings.*field = !(m_settings.*field);
            translationOptionsChanged();
        }, nullptr, hint);
        command(QString::fromLatin1(id))->action->setCheckable(true);
    };
    toggle("translate-on-select", tr("Translate When Text Is Selected"), &Settings::translateOnSelect,
           tr("Off: selecting only selects. Right-click > Translate, or Ctrl+E."));
    toggle("use-gemini", tr("Use Gemini (Online)"), &Settings::useGemini,
           tr("Google's Gemini, asked first. The only engine that sends text over the internet: off, nothing is sent."));
    addCommand(QStringLiteral("gemini-explain-only"), tr("Use Gemini Only for Explain This Sentence"), QKeySequence(), [this] {
        m_settings.geminiExplainOnly = !m_settings.geminiExplainOnly;
        translationOptionsChanged();
    }, [this] { return m_settings.useGemini; },
       tr("Words, passages and the paragraphs alongside come from the engines after Gemini (the offline dictionaries "
          "and translator, or LM Studio when it's on)."));
    command(QStringLiteral("gemini-explain-only"))->action->setCheckable(true);
    toggle("use-lm-studio", tr("Use LM Studio (This Computer)"), &Settings::useLmStudio, tr("Asked when Gemini can't answer, or takes too long (Settings > Translation)."));
    toggle("use-dictionary", tr("Use the Offline Dictionary"), &Settings::useDictionary, tr("Single words, when nothing before it in the list answers."));
    toggle("use-offline-translator", tr("Use the Offline Translator"), &Settings::useOfflineTranslator,
           tr("Sentences and pages with no internet, when Gemini and LM Studio can't answer (a download in Settings)."));
    addCommand(QStringLiteral("getting-started"), tr("Getting Started"), QKeySequence(Qt::SHIFT | Qt::Key_F1),
               [this] { setHelpVisible(!helpVisible()); }, nullptr,
               tr("A guide beside the page: every feature and how to use it, common settings, questions and answers."));
    command(QStringLiteral("getting-started"))->action->setCheckable(true);
    addCommand(QStringLiteral("keyboard-shortcuts"), tr("Keyboard Shortcuts…"), QKeySequence(), [this] { showSettings(SettingsDialog::ShortcutsTab); },
               nullptr, tr("Every shortcut, and changing them."));
    applyShortcuts();
    qApp->installEventFilter(this);   // mouse-button shortcuts
}

QString MainWindow::whyUnavailable(const QString& id) const
{
    if (id == QLatin1String("new-collection") || id == QLatin1String("library-grid") || id == QLatin1String("library-list"))
        return tr("Only in the library.");
    if (id == QLatin1String("gemini-explain-only")) return tr("Gemini is off (Settings > Translation).");
    if (!isReading()) return tr("Open a book first.");
    if (id == QLatin1String("back")) return tr("Nowhere to go back to yet: Back returns to where you were before a jump (a link, a search result, Go to Page).");
    if (id == QLatin1String("copy") || id == QLatin1String("translate") || id == QLatin1String("explain"))
        return tr("Select some text in the book first.");
    if (id == QLatin1String("book-offline")) return tr("Gemini is off (Settings > Translation): no book's text is sent online.");
    return {};
}

void MainWindow::buildMenus()
{
    QMenuBar* bar = menuBar();
    bar->setNativeMenuBar(false);
    auto add = [this](QMenu* m, const char* id) {
        if (Command* c = command(QString::fromLatin1(id))) m->addAction(c->action);
    };
    // A command that can't run now is greyed out while its menu is open; its shortcut keeps working the rest of the
    // time (the command checks again when it runs).
    // Hovering a greyed-out command says why it can't run now (the owner's request, 2026-11-26).
    auto watch = [this](QMenu* m) {
        connect(m, &QMenu::aboutToShow, this, [this] {
            for (auto& c : m_commands) {
                const bool on = c->isEnabled();
                c->action->setEnabled(on);
                c->action->setToolTip(on ? QString() : whyUnavailable(c->id));
            }
            syncChecks();
        });
        connect(m, &QMenu::aboutToHide, this, [this] {
            for (auto& c : m_commands) {
                c->action->setEnabled(true);
                c->action->setToolTip(QString());
            }
        });
    };

    QMenu* file = bar->addMenu(tr("&File"));
    file->setProperty("testId", "menuFile");
    add(file, "open");
    add(file, "add-books");
    add(file, "add-folder");
    add(file, "new-collection");
    add(file, "import-kindle");
    file->addSeparator();
    add(file, "library");
    file->addSeparator();
    add(file, "quit");
    watch(file);

    QMenu* view = bar->addMenu(tr("&View"));
    view->setProperty("testId", "menuView");
    add(view, "contents");
    add(view, "notebook");
    add(view, "bilingual");
    add(view, "pages-right-to-left");
    view->addSeparator();
    add(view, "larger");
    add(view, "smaller");
    add(view, "default-size");
    QMenu* margins = view->addMenu(tr("Margins"));
    margins->setProperty("testId", "menuMargins");
    add(margins, "wider-margins");
    add(margins, "narrower-margins");
    add(margins, "default-margins");
    QMenu* columns = view->addMenu(tr("Columns"));
    add(columns, "columns-one");
    add(columns, "columns-two");
    add(columns, "columns-auto");
    view->addSeparator();
    add(view, "library-grid");
    add(view, "library-list");
    view->addSeparator();
    add(view, "fullscreen");
    watch(view);

    QMenu* go = bar->addMenu(tr("&Go"));
    go->setProperty("testId", "menuGo");
    add(go, "goto");
    add(go, "back");
    go->addSeparator();
    add(go, "next-chapter");
    add(go, "previous-chapter");
    add(go, "book-start");
    go->addSeparator();
    add(go, "search-book");
    add(go, "search-library");
    add(go, "bookmark");
    add(go, "latest-bookmark");
    go->addSeparator();
    add(go, "practice");
    add(go, "statistics");
    add(go, "word-list");
    watch(go);

    QMenu* settings = bar->addMenu(tr("&Settings"));
    settings->setProperty("testId", "menuSettings");
    add(settings, "appearance");
    QMenu* translation = settings->addMenu(tr("Translation"));
    translation->setProperty("testId", "menuTranslation");
    add(translation, "translate-on-select");
    translation->addSeparator();
    add(translation, "use-gemini");
    add(translation, "gemini-explain-only");
    add(translation, "use-lm-studio");
    add(translation, "use-dictionary");
    add(translation, "use-offline-translator");
    translation->addSeparator();
    add(translation, "book-offline");
    translation->addSeparator();
    add(translation, "translation-settings");
    watch(translation);
    m_themeMenu = settings->addMenu(tr("Theme"));
    m_themeMenu->setProperty("testId", "menuTheme");
    connect(m_themeMenu, &QMenu::aboutToShow, this, &MainWindow::fillThemeMenu);
    // Pointing at a theme previews it on the window; leaving the menu without choosing puts the saved one back.
    connect(m_themeMenu, &QMenu::aboutToHide, this, [this] { previewTheme(QString()); });
    // The Settings window's tabs, in its order (General, Translation's quick switches above, Audio, Keyboard
    // Shortcuts). Change App Data Folder is in General.
    settings->addSeparator();
    add(settings, "settings");
    add(settings, "audio-settings");
    add(settings, "keyboard-shortcuts");
    watch(settings);

    QMenu* help = bar->addMenu(tr("&Help"));
    help->setProperty("testId", "menuHelp");
    add(help, "getting-started");
    add(help, "commands");
    help->addSeparator();
    add(help, "about");
    watch(help);
}

void MainWindow::fillThemeMenu()
{
    m_themeMenu->clear();
    auto* group = new QActionGroup(m_themeMenu);
    auto addTheme = [this, group](QMenu* m, const QString& key, const QString& name) {
        QAction* a = m->addAction(name);
        a->setCheckable(true);
        a->setChecked(key == m_settings.theme);
        a->setData(key);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, key] { chooseTheme(key); });
        connect(a, &QAction::hovered, this, [this, key] { previewTheme(key); });
    };
    // The theme groups (each a submenu), then the choices without a submenu, then the theme list.
    for (const QString& g : Theme::groups()) {
        QMenu* sub = m_themeMenu->addMenu(g);
        connect(sub, &QMenu::aboutToHide, this, [this] { previewTheme(QString()); });
        for (const ThemeDef& d : themeTable())
            if (d.group == g) addTheme(sub, d.key, d.name);
    }
    m_themeMenu->addSeparator();
    addTheme(m_themeMenu, QStringLiteral("system"), tr("Match Operating System"));
    m_themeMenu->addSeparator();
    if (Command* c = command(QStringLiteral("themes"))) {
        c->action->setText(tr("Theme List…"));
        m_themeMenu->addAction(c->action);
    }
}

void MainWindow::applyShortcuts()
{
    // Second keys people expect, used while the command keeps its default.
    static const QHash<QString, QList<QKeySequence>> extra = {
        {QStringLiteral("commands"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), QKeySequence(Qt::Key_F1)}},
        {QStringLiteral("larger"), {QKeySequence(Qt::CTRL | Qt::Key_Plus)}},
        {QStringLiteral("back"), {QKeySequence(Qt::Key_Backspace)}},
    };
    for (auto& c : m_commands) {
        QKeySequence keys = c->defaultShortcut;
        bool changed = false;
        // Changes from Settings: "id=Ctrl+X" ("id=" for none).
        for (const QString& o : m_settings.shortcuts)
            if (o.section(QLatin1Char('='), 0, 0) == c->id) {
                keys = QKeySequence(o.section(QLatin1Char('='), 1), QKeySequence::PortableText);
                changed = true;
            }
        c->shortcut = keys;
        c->mouse.clear();
        for (const QString& o : m_settings.mouseShortcuts)
            if (o.section(QLatin1Char('='), 0, 0) == c->id) c->mouse = o.section(QLatin1Char('='), 1);
        QList<QKeySequence> all;
        if (!keys.isEmpty()) all << keys;
        if (!changed) all << extra.value(c->id);
        c->action->setShortcuts(all);
    }
}

QString MainWindow::translatorFolder() { return QDir(appdata::folder()).filePath(QStringLiteral("translator")); }

KindleImportDialog* MainWindow::kindleDialog() const { return m_kindleDialog; }

void MainWindow::importKindle(const QString& fileArg)
{
    const core::Book* book = m_engine->book();
    if (!isReading() || !book) return;
    QString file = fileArg;
    if (file.isEmpty()) {
        if (!m_interactive) return;
        file = QFileDialog::getOpenFileName(this, tr("Import Highlights"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                            tr("Highlights (*.json *.txt *.md *.csv *.tsv);;Kindle (*.json *.txt);;"
                                               "FreeReader Notebook (*.md *.txt *.csv *.tsv *.json);;All files (*)"));
        if (file.isEmpty()) return;
    }
    QFile f(file);
    QString error;
    core::KindleExport ex;
    if (!f.open(QIODevice::ReadOnly)) error = tr("The file can't be read: %1").arg(f.errorString());
    else ex = core::parseKindleExport(f.readAll(), book->metadata.title);
    if (error.isEmpty()) error = ex.error;
    if (!error.isEmpty()) {
        m_lastError = error;
        if (m_interactive) QMessageBox::information(this, tr("Import Highlights"), error);
        return;
    }
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    // Equivalents for the highlights this edition words differently (another form of the word, or the matching spot).
    const core::KindlePlan plan = core::planKindleImport(*book, ex, nullptr, true);
    QGuiApplication::restoreOverrideCursor();
    if (m_kindleDialog) m_kindleDialog->deleteLater();
    m_kindleDialog = new KindleImportDialog(ex, plan, book->metadata.title,
                                            [this, plan](const ReaderView::KindleOptions& o) { return m_reader->importKindle(plan, o); }, this);
    m_kindleDialog->setAttribute(Qt::WA_DeleteOnClose);
    if (m_interactive) m_kindleDialog->open();
}

QString MainWindow::dictionaryFile(const QString& key)
{
    // "es": Spanish -> English (es-en.sqlite), "en": English definitions (en-en.sqlite), "es-es": Spanish definitions.
    const QString name = key.contains(QLatin1Char('-')) ? key : key + QStringLiteral("-en");
    return QDir(appdata::folder()).filePath(QStringLiteral("dictionaries/%1.sqlite").arg(name));
}

void MainWindow::applyTranslationSettings()
{
    m_dictionary->setEnabled(m_settings.useDictionary);
    if (!m_dictionary->info().ok) m_dictionary->open(dictionaryFile(QStringLiteral("es")));
    m_dictionaryEn->setEnabled(m_settings.useDictionary);
    if (!m_dictionaryEn->info().ok) m_dictionaryEn->open(dictionaryFile(QStringLiteral("en")));
    m_definitionsEs->setEnabled(m_settings.useDictionary);
    if (!m_definitionsEs->info().ok) m_definitionsEs->open(dictionaryFile(QStringLiteral("es-es")));
    m_dictionaryEnEs->setEnabled(m_settings.useDictionary);
    if (!m_dictionaryEnEs->info().ok) m_dictionaryEnEs->open(dictionaryFile(QStringLiteral("en-es")));
    m_offline->setEnabled(m_settings.useOfflineTranslator);
    m_offline->setFolder(translatorFolder());
    m_gemini->setEnabled(m_settings.useGemini);
    m_gemini->setModel(m_settings.geminiModel);
    m_gemini->setExplainOnly(m_settings.geminiExplainOnly);
    m_lmStudio->setEnabled(m_settings.useLmStudio);
    m_lmStudio->setAddress(m_settings.lmStudioAddress);
    m_lmStudio->setModel(m_settings.lmStudioModel);
    // How long a word or passage waits for each before the next engine answers (the owner's requests, 2026-11-26).
    m_translator->setWait(m_gemini->name(), qMax(0, m_settings.geminiWaitMs));
    m_translator->setWait(m_lmStudio->name(), qMax(0, m_settings.lmStudioWaitMs));
    m_translator->clearPauses();
    syncChecks();
}

void MainWindow::translationOptionsChanged()
{
    applyTranslationSettings();
    saveSettingsSoon();
    if (m_settingsDialog) m_settingsDialog->refreshTranslation();
}

void MainWindow::syncChecks()
{
    auto set = [this](const char* id, bool on) {
        if (Command* c = command(QString::fromLatin1(id))) c->action->setChecked(on);
    };
    set("translate-on-select", m_settings.translateOnSelect);
    set("use-gemini", m_settings.useGemini);
    set("gemini-explain-only", m_settings.geminiExplainOnly);
    set("use-lm-studio", m_settings.useLmStudio);
    set("use-dictionary", m_settings.useDictionary);
    set("use-offline-translator", m_settings.useOfflineTranslator);
    set("book-offline", m_reader && m_reader->bookIsOffline());
    set("bilingual", m_reader && m_reader->bilingualVisible());
    set("pages-right-to-left", m_reader && isReading() && m_reader->pagesRightToLeft());
}

WelcomeDialog* MainWindow::welcome() const { return m_welcome; }

void MainWindow::showWelcome()
{
    ensureSettingsDialog();   // it knows which packs are installed, and downloads them
    if (m_welcome) m_welcome->deleteLater();
    m_welcomePacks.clear();
    m_welcome = new WelcomeDialog(
        m_settings, [this](const QString& language) { return m_settingsDialog->isPackInstalled(language); }, appdata::folder(), this);
    m_welcome->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_welcome, &WelcomeDialog::downloadRequested, this, [this](const QStringList& languages) { m_welcomePacks = languages; });
    connect(m_welcome, &QDialog::finished, this, [this] {
        saveSettingsNow();
        // After the welcome has closed (its download request comes right after): in another language the window is
        // rebuilt in it, and the new one downloads; otherwise this one does.
        QTimer::singleShot(0, this, [this] {
            const QStringList packs = std::exchange(m_welcomePacks, {});
            if (interfaceLanguage() != m_builtLanguage) emit interfaceLanguageChanged(currentBookPath(), packs);
            else if (!packs.isEmpty()) downloadPacks(packs);
        });
    });
    if (m_interactive) m_welcome->open();
}

void MainWindow::downloadPacks(const QStringList& languages)
{
    // The downloads run in Settings > General, where their progress shows (and Cancel Download).
    showSettings(SettingsDialog::GeneralTab);
    m_settingsDialog->downloadPacks(languages);
}

QString MainWindow::currentBookPath() const
{
    if (!isReading() || !m_library || !m_reader->bookId()) return {};
    return m_library->book(m_reader->bookId()).path;
}

void MainWindow::prepareToClose()
{
    m_reader->closeBook();   // keeps the place in the book
    saveSettingsNow();
}

void MainWindow::showSettings(int tab)
{
    ensureSettingsDialog();
    if (!m_settingsDialog->isVisible()) m_settingsDialog->begin();
    m_settingsDialog->setDataFolder(appdata::folder(), appdata::isTemporary());
    m_settingsDialog->refreshTranslation();
    m_settingsDialog->fillLibraryFolders();
    m_settingsDialog->showTab(SettingsDialog::Tab(tab));
    m_settingsDialog->show();
    m_settingsDialog->raise();
}

void MainWindow::ensureSettingsDialog()
{
    if (!m_settingsDialog) {
        std::vector<Command*> list;
        for (auto& c : m_commands) list.push_back(c.get());
        m_settingsDialog = new SettingsDialog(m_settings, list, m_gemini, m_lmStudio, m_interactive, this);
        m_settingsDialog->setDictionary(m_dictionary, dictionaryFile(QStringLiteral("es")));
        m_settingsDialog->setEnglishDictionary(m_dictionaryEn, dictionaryFile(QStringLiteral("en")));
        m_settingsDialog->setDictionaryFor(QStringLiteral("es-es"), m_definitionsEs, dictionaryFile(QStringLiteral("es-es")));
        m_settingsDialog->setDictionaryFor(QStringLiteral("en-es"), m_dictionaryEnEs, dictionaryFile(QStringLiteral("en-es")));
        m_settingsDialog->setInterfaceLanguage(m_builtLanguage);
        // Settings > General > Language > Restart Now: the window again, in the language chosen.
        connect(m_settingsDialog, &SettingsDialog::restartRequested, this, [this] {
            saveSettingsNow();
            emit interfaceLanguageChanged(currentBookPath(), {});
        });
        m_settingsDialog->setOfflineTranslator(m_offline);
        // The book open now, for Keep This Book Offline in the window (0: none).
        m_settingsDialog->setOpenBook([this]() -> std::pair<int, QString> {
            if (!isReading() || !m_reader->bookId()) return {0, QString()};
            return {m_reader->bookId(), m_library->book(m_reader->bookId()).title};
        });
        m_settingsDialog->setVoiceSource(
            [this] {
                QList<SettingsDialog::Voice> out;
                for (const ReaderView::SpeechVoice& v : m_reader->speechVoices()) out << SettingsDialog::Voice{v.name, v.locale};
                return out;
            },
            [this](const QString& language) { return m_reader->playVoiceSample(language); });
        connect(m_settingsDialog, &SettingsDialog::audioChanged, this, [this] { saveSettingsSoon(); });
        connect(m_settingsDialog, &SettingsDialog::translationChanged, this, [this] {
            applyTranslationSettings();
            saveSettingsSoon();
        });
        connect(m_settingsDialog, &SettingsDialog::libraryFoldersChanged, this, &MainWindow::saveSettingsSoon);
        connect(m_settingsDialog, &SettingsDialog::shortcutsChanged, this, [this] {
            applyShortcuts();
            saveSettingsSoon();
        });
        connect(m_settingsDialog, &SettingsDialog::resetTextAndLayoutRequested, this, [this] {
            m_settings.look = Appearance();
            if (isReading()) m_reader->applyReadingOptions();
            saveSettingsSoon();
        });
        connect(m_settingsDialog, &SettingsDialog::changeDataFolderRequested, this, [this] {
            changeDataFolder();
            m_settingsDialog->setDataFolder(appdata::folder(), appdata::isTemporary());
        });
        connect(m_settingsDialog, &SettingsDialog::addLibraryFolderRequested, this, [this] {
            if (m_interactive) m_start->chooseFolderToAdd();
            m_settingsDialog->fillLibraryFolders();
            saveSettingsSoon();
        });
        // Cancel put the settings back: apply them all again.
        connect(m_settingsDialog, &SettingsDialog::cancelled, this, [this] {
            applyTranslationSettings();
            applyShortcuts();
            if (isReading()) m_reader->applyReadingOptions();
            m_reader->applyPageTurnSettings();
            saveSettingsSoon();
        });
        connect(m_settingsDialog, &SettingsDialog::pageTurnChanged, this, [this] {
            m_reader->applyPageTurnSettings();   // the page and the Text and Layout panel
            saveSettingsSoon();
        });
        connect(m_settingsDialog, &SettingsDialog::languageChanged, this, &MainWindow::saveSettingsSoon);
        connect(m_settingsDialog, &SettingsDialog::packFinished, this, [this] {
            applyTranslationSettings();
            saveSettingsSoon();
        });
    }
}

Command* MainWindow::command(const QString& id) const
{
    for (const auto& c : m_commands)
        if (c->id == id) return c.get();
    return nullptr;
}

bool MainWindow::helpVisible() const { return m_help && !m_help->isHidden(); }

void MainWindow::setHelpVisible(bool on)
{
    if (!m_help) return;
    // One panel on the right at a time, as in the reader: the guide takes the place of the Notebook, English
    // alongside or the practice workspace (both at once left the page a narrow column and widened the window).
    if (on && m_reader) {
        if (m_reader->notebookVisible()) m_reader->setNotebookVisible(false);
        if (m_reader->bilingualVisible()) m_reader->setBilingualVisible(false);
        if (m_reader->practiceVisible()) m_reader->setPracticeVisible(false);
        syncChecks();
    }
    if (on) placeHelp();   // beside or over the screen, decided before it shows (shown beside it first, the window grew)
    m_help->setVisible(on);
    if (on) placeHelp();
    if (Command* c = command(QStringLiteral("getting-started"))) c->action->setChecked(on);
}

void MainWindow::placeHelp()
{
    if (!m_help || !m_body) return;
    // Beside the screen when there is room for both; otherwise over the screen's right side, like a drawer (the
    // reader and the library need about 800 px: in a 1024 px window the guide beside them made the window wider).
    const int want = 420;
    const bool room = m_body->width() - want >= m_stack->minimumSizeHint().width();
    const bool docked = m_bodyLay->indexOf(m_help) >= 0;
    if (room) {
        // Beside: 420 px while there is room, able to shrink so the window can still be made smaller (then it
        // goes over the screen at the next resize).
        m_help->setGraphicsEffect(nullptr);
        m_help->setMinimumWidth(0);
        m_help->setMaximumWidth(want);
        if (!docked) m_bodyLay->addWidget(m_help);
    } else {
        if (docked) m_bodyLay->removeWidget(m_help);
        const int w = qMin(want, qMax(280, m_body->width() - 80));
        // Below the screen's top bar, so its buttons (Practice, Search, Add Books…) stay in reach.
        int top = 0;
        if (QWidget* screen = m_stack->currentWidget())
            if (auto* bar = screen->findChild<QWidget*>(QStringLiteral("Bar"), Qt::FindDirectChildrenOnly); bar && bar->isVisible() && bar->y() < 80)
                top = bar->mapTo(m_body, QPoint(0, bar->height())).y();
        m_help->setFixedWidth(w);
        m_help->setGeometry(m_body->width() - w, top, w, m_body->height() - top);
        if (!m_help->graphicsEffect()) {
            auto* shadow = new QGraphicsDropShadowEffect(m_help);
            shadow->setBlurRadius(28);
            shadow->setOffset(-6, 0);
            shadow->setColor(QColor(0, 0, 0, 90));
            m_help->setGraphicsEffect(shadow);
        }
        if (m_help->isVisible()) m_help->raise();
    }
}

bool MainWindow::helpOverlays() const { return m_help && m_bodyLay && m_bodyLay->indexOf(m_help) < 0; }

void MainWindow::runCommand(const QString& id)
{
    if (Command* c = command(id))
        if (c->isEnabled() && c->run) c->run();
}

bool MainWindow::isReading() const { return m_stack->currentWidget() == m_reader && m_engine->isOpen(); }

bool MainWindow::isSettled() const
{
    if (!m_openingPath.isEmpty() || m_start->isImporting()) return false;
    return m_stack->currentWidget() == m_start || m_stack->currentWidget() == m_practice || m_stack->currentWidget() == m_librarySearch ||
           m_stack->currentWidget() == m_statistics ||
           m_reader->isSettled();
}

void MainWindow::chooseBook()
{
    if (!m_interactive) return;
    const QStringList suffixes = core::FormatRegistry::instance().fileSuffixes();
    QStringList patterns;
    for (const QString& s : suffixes) patterns << QStringLiteral("*.") + s;
    const QString dir = m_settings.lastFolder.isEmpty() ? QDir::homePath() : m_settings.lastFolder;
    const QString f = QFileDialog::getOpenFileName(this, tr("Open Book"), dir, tr("E-books (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (f.isEmpty()) return;
    m_settings.lastFolder = QFileInfo(f).absolutePath();
    saveSettingsSoon();
    openBook(f);
}

void MainWindow::openBook(const QString& path)
{
    if (m_engine->isOpen()) m_reader->closeBook();
    m_openingPath = path;
    m_lastError.clear();
    m_start->setMessage(tr("Opening %1…").arg(QFileInfo(path).fileName()));
    m_engine->open(path);
}

void MainWindow::onOpened()
{
    const core::Book* book = m_engine->book();
    const QString path = m_openingPath;
    m_openingPath.clear();
    m_start->setMessage(QString());
    int id = m_library ? m_library->touchBook(path, book->metadata) : 0;
    // A book opened directly (not added through the library) gets its cover thumbnail now.
    if (m_library && id && !m_library->book(id).hasCover) {
        const QByteArray cover = core::Importer::thumbnail(*book);
        if (!cover.isEmpty()) m_library->addBook(path, book->metadata, cover);
    }
    core::Position start;
    core::SavedPosition saved;
    if (m_library && m_library->loadPosition(id, &saved)) {
        // The CFI survives changes to the app and the book file; the stored offset is the fallback.
        if (saved.cfi.isEmpty() || !m_engine->positionForCfi(saved.cfi, &start)) start = saved.position;
    }
    if (start.spine < 0 || start.spine >= int(book->spine.size())) start = {};
    setWindowTitle(book->metadata.title + QStringLiteral(" – ") + QStringLiteral(APP_NAME));
    m_stack->setCurrentWidget(m_reader);
    m_reader->showBook(id, start);
    if (m_pendingJump.spine >= 0) applyPendingJump();
    if (m_settingsDialog) m_settingsDialog->refreshOfflineBooks();   // its Keep This Book Offline names the book
    emit bookShown();
}

void MainWindow::onOpenFailed(const QString& message)
{
    const QString name = QFileInfo(m_openingPath).fileName();
    m_openingPath.clear();
    m_pendingJump = {};
    m_lastError = message;
    m_stack->setCurrentWidget(m_start);
    m_start->setMessage(tr("Couldn't open %1. %2").arg(name, message));
    emit bookFailed(message);
}

void MainWindow::showLibrary()
{
    if (m_stack->currentWidget() != m_start) {
        m_reader->closeBook();
        m_engine->close();
        setWindowTitle(QStringLiteral(APP_NAME));
        m_stack->setCurrentWidget(m_start);
        if (m_settingsDialog) m_settingsDialog->refreshOfflineBooks();
    }
    m_start->refresh();
}

void MainWindow::changeTextSize(int delta)
{
    // In the library: the covers' size; while reading: the text size.
    if (m_stack->currentWidget() == m_start) {
        m_start->zoomTo(delta == 0 ? 100 : m_settings.libraryZoomPct + delta);
        return;
    }
    m_reader->stepTextSize(delta);
}

void MainWindow::setColumns(const QString& mode) { m_reader->changeAppearance(QStringLiteral("columns"), mode); }

void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        m_wasMaximized ? showMaximized() : showNormal();
    } else {
        m_wasMaximized = isMaximized();
        showFullScreen();
    }
    menuBar()->setVisible(!isFullScreen());
    m_reader->setFullScreenIcon(isFullScreen());
}

void MainWindow::changeDataFolder()
{
    if (!m_interactive) return;
    QString message;
    if (appdata::change(this, &message)) {
        m_start->setSettingsNotKept(appdata::isTemporary());
        m_reader->setSettingsNotKept(appdata::isTemporary());
        QMessageBox::information(this, tr("App Data Folder"),
                                 message + QLatin1Char('\n') + tr("The library database moves with the next start of the app."));
    } else if (!message.isEmpty()) {
        QMessageBox::warning(this, tr("App Data Folder"), message);
    }
}

void MainWindow::about()
{
    if (!m_interactive) return;
    QMessageBox::about(this, tr("About FreeReader"),
                       tr("<b>%1</b> %2<br>An e-book reader for DRM-free books.<br><br>Copyright (C) 2026 cincottash<br>"
                          "Licensed under the GNU General Public License v3.0.")
                           .arg(QStringLiteral(APP_NAME), QStringLiteral(APP_VERSION)));
}

void MainWindow::saveSettingsSoon() { m_settingsTimer->start(); }

void MainWindow::saveSettingsNow()
{
    if (!isFullScreen()) m_settings.geometry = saveGeometry();
    m_settings.save(appdata::settingsFile());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_interactive) startupTrace("closing: book");
    m_reader->closeBook();
    if (m_interactive) startupTrace("closing: settings");
    saveSettingsNow();
    if (m_interactive) startupTrace("closing: done");
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasUrls()) return;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && core::FormatRegistry::instance().fileSuffixes().contains(QFileInfo(u.toLocalFile()).suffix().toLower())) {
            e->acceptProposedAction();
            return;
        }
}

void MainWindow::dropEvent(QDropEvent* e)
{
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) {
            openBook(u.toLocalFile());
            return;
        }
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);
    if (m_palette->isVisible()) m_palette->place();
    if (m_imageViewer->isVisible()) m_imageViewer->setGeometry(m_stack->rect());
    saveSettingsSoon();
}

void MainWindow::showTheme(const Theme& t, bool preview)
{
    if (preview) {
        // Only the screen in view is restyled for a preview: restyling the whole window (the hidden library,
        // practice, statistics, ...) was most of a preview's time and made the theme list lag (the owner's report,
        // 2026-11-23). The rest follows when a theme is chosen.
        QWidget* target = m_stack->currentWidget();
        if (m_previewTarget && m_previewTarget != target) clearThemeFrom(m_previewTarget);
        m_previewTarget = target;
        applyThemeTo(target, t);
        if (target == m_reader) m_reader->setTheme(t);
        else if (target == m_start) m_start->setTheme(t);
        else if (target == m_practice) m_practice->setTheme(t);
        else if (target == m_librarySearch) m_librarySearch->setTheme(t);
        else if (target == m_statistics) m_statistics->setTheme(t);
        else if (target == m_imageViewer) m_imageViewer->setTheme(t);
        return;
    }
    endPreview();
    applyTheme(*qApp, t);
    m_theme = t;
    m_help->setTheme(t);
    m_reader->setTheme(t);
    m_start->setTheme(t);
    m_practice->setTheme(t);
    m_librarySearch->setTheme(t);
    m_statistics->setTheme(t);
    m_imageViewer->setTheme(t);
}

void MainWindow::endPreview()
{
    if (!m_previewTarget) return;
    clearThemeFrom(m_previewTarget);
    m_previewTarget = nullptr;
}

void MainWindow::showPractice(bool wordList)
{
    if (m_stack->currentWidget() != m_practice) m_beforePractice = m_stack->currentWidget();
    m_practice->setCanShowBesideBook(m_beforePractice == m_reader && m_engine->isOpen());
    m_stack->setCurrentWidget(m_practice);
    wordList ? m_practice->showList() : m_practice->showStart();
}

void MainWindow::showStatistics()
{
    if (m_stack->currentWidget() != m_statistics) m_beforeStatistics = m_stack->currentWidget();
    // The reading time so far counts too.
    if (isReading()) m_reader->flushReading();
    m_statistics->refresh();
    m_stack->setCurrentWidget(m_statistics);
    m_statistics->setFocus();
}

void MainWindow::showLibrarySearch(const QString& query)
{
    if (m_stack->currentWidget() != m_librarySearch) m_beforeLibrarySearch = m_stack->currentWidget();
    m_stack->setCurrentWidget(m_librarySearch);
    if (!query.simplified().isEmpty()) m_librarySearch->setQuery(query);
    m_librarySearch->setFocus();
    if (auto* box = m_librarySearch->findChild<QLineEdit*>()) box->setFocus();
}

void MainWindow::goToLatestBookmark()
{
    const core::Annotation b = m_annotations ? m_annotations->latestBookmark() : core::Annotation();
    if (b.id == 0) {
        const QString none = tr("No bookmarks yet. Ctrl+D bookmarks the page you're reading.");
        if (isReading()) m_reader->pages()->showNotice(none);
        else m_start->setMessage(none);
        return;
    }
    const core::LibraryBook book = m_library ? m_library->book(b.bookId) : core::LibraryBook();
    if (book.id == 0 || !QFileInfo::exists(book.path)) {
        m_start->setMessage(tr("The book with your latest bookmark wasn't found."));
        m_stack->setCurrentWidget(m_start);
        return;
    }
    // The bookmark's page (a word bookmark: its word's page, the word not selected, so no translation opens).
    m_pendingJump = {b.cfi, b.spine, b.start, b.start};
    m_reader->setPracticeMaximized(false);
    if (m_engine->isOpen() && m_reader->bookId() == b.bookId) {
        m_stack->setCurrentWidget(m_reader);
        applyPendingJump();
        m_reader->pages()->setFocus();
    } else {
        openBook(book.path);
    }
}

void MainWindow::applyPendingJump()
{
    const PendingJump j = m_pendingJump;
    m_pendingJump = {};
    core::Position p{j.spine, j.start};
    if (!j.cfi.isEmpty()) m_engine->positionForCfi(j.cfi, &p);
    m_reader->jumpTo(p);
    if (j.end > j.start) m_reader->pages()->selectRange({p.spine, p.offset}, {p.spine, p.offset + (j.end - j.start)});
}

void MainWindow::chooseTheme(const QString& key)
{
    m_previewKey.clear();
    // The theme being left stays one click away as a dot.
    if (!m_settings.recentThemes.contains(m_settings.theme)) m_settings.recentThemes.prepend(m_settings.theme);
    m_settings.theme = key;
    m_settings.recentThemes.removeAll(key);
    m_settings.recentThemes.prepend(key);
    while (m_settings.recentThemes.size() > 4) m_settings.recentThemes.removeLast();
    showTheme(Theme::forChoice(key), false);
    saveSettingsSoon();
}

void MainWindow::previewTheme(const QString& key)
{
    if (key == m_previewKey) return;
    m_previewKey = key;
    if (key.isEmpty() || key == m_settings.theme) {
        // Back to the saved theme (only if a preview was showing).
        if (m_previewTarget) {
            endPreview();
            m_reader->setTheme(m_theme);
            m_start->setTheme(m_theme);
            m_practice->setTheme(m_theme);
            m_librarySearch->setTheme(m_theme);
            m_statistics->setTheme(m_theme);
            m_imageViewer->setTheme(m_theme);
        }
        return;
    }
    showTheme(Theme::forChoice(key), true);
}

SettingsDialog* MainWindow::settingsDialog() const { return m_settingsDialog.data(); }

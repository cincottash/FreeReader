#include "MainWindow.h"

#include "AppData.h"
#include "Popups.h"
#include "ReaderView.h"
#include "LibraryView.h"
#include "PracticeView.h"
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
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
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
    // When Gemini and LM Studio can't answer: the meaning saved in the Word List from an earlier lookup, the
    // offline dictionaries (words), then the offline translator (sentences and passages).
    m_savedWords = new core::SavedWords(this);
    m_savedWords->setDictionaries({m_dictionary, m_dictionaryEn});
    m_offline = new core::OfflineTranslator(this);
    m_offline->setFolder(translatorFolder());
    m_translator->setProviders({m_gemini, m_lmStudio, m_savedWords, m_dictionary, m_dictionaryEn, m_offline});
    m_translator->setInstant({m_dictionary, m_dictionaryEn});
    m_gemini->setKey(core::secrets::read(QStringLiteral("gemini-api-key")));
    m_stack = new QStackedWidget;
    m_start = new LibraryView(m_library, m_settings);
    m_reader = new ReaderView(m_engine, m_library, m_settings);
    m_reader->setInteractive(interactive);
    m_reader->setTranslator(m_translator);
    m_words = std::make_unique<core::WordLog>(m_library);
    m_reader->setWordLog(m_words.get());
    m_reader->setOfflineTranslator(m_offline);
    m_savedWords->setWordLog(m_words.get());
    m_annotations = std::make_unique<core::Annotations>(m_library);
    m_reader->setAnnotations(m_annotations.get());
    m_practice = new PracticeView(m_words.get(), m_settings);
    m_practice->setInteractive(interactive);
    m_practice->setTranslator(m_translator);
    m_stack->addWidget(m_practice);
    m_stack->addWidget(m_start);
    m_stack->addWidget(m_reader);
    m_librarySearch = new LibrarySearchView(m_library);
    m_stack->addWidget(m_librarySearch);
    m_statistics = new StatisticsView(m_library);
    m_stack->addWidget(m_statistics);
    m_stack->setCurrentWidget(m_start);   // the library first, whatever order the screens were added in
    setCentralWidget(m_stack);
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
    addCommand(QStringLiteral("settings"), tr("Settings…"), QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { showSettings(); }, nullptr,
               tr("App data folder, keyboard shortcuts, resets."));
    addCommand(QStringLiteral("translate"), tr("Translate Selection"), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { m_reader->translateSelection(); },
               [this] { return isReading() && m_reader->pages()->hasSelection(); }, tr("Shows the translation pop-up for the selected text."));
    addCommand(QStringLiteral("explain"), tr("Explain This Sentence"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), [this] { m_reader->explainSelection(); },
               [this] { return isReading() && m_reader->pages()->hasSelection(); },
               tr("The selected sentence in English, with notes on its grammar and idioms."));
    addCommand(QStringLiteral("book-offline"), tr("Keep This Book Offline"), QKeySequence(), [this] {
        m_reader->setBookOffline(!m_reader->bookIsOffline());
    }, [this] { return isReading() && !m_settings.offlineOnly; }, tr("Text from this book is never sent to Gemini."));
    if (Command* c = command(QStringLiteral("book-offline"))) c->action->setCheckable(true);
    addCommand(QStringLiteral("import-kindle"), tr("Import Kindle Highlights…"), QKeySequence(), [this] { importKindle(); }, reading,
               tr("Highlights, notes and bookmarks from a Kindle export (Bookcision's JSON or My Clippings.txt) into this book."));
    addCommand(QStringLiteral("search-book"), tr("Search This Book…"), QKeySequence(Qt::CTRL | Qt::Key_F), [this] {
        m_reader->openSearch(m_reader->pages()->hasSelection() ? m_reader->pages()->selectedText() : QString());
    }, reading, tr("Every place a word or phrase appears, with its page."));
    addCommand(QStringLiteral("search-library"), tr("Search All Books…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), [this] {
        showLibrarySearch(isReading() && m_reader->pages()->hasSelection() ? m_reader->pages()->selectedText() : QString());
    }, nullptr, tr("A word or phrase in the text of every book in the library."));
    addCommand(QStringLiteral("bookmark"), tr("Bookmark This Page"), QKeySequence(Qt::CTRL | Qt::Key_D), [this] { m_reader->toggleBookmark(); },
               reading, tr("Adds a bookmark, or removes the one on this page."));
    addCommand(QStringLiteral("notebook"), tr("Show or Hide Notebook"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
               [this] { m_reader->setNotebookVisible(!m_reader->notebookVisible()); saveSettingsSoon(); }, reading,
               tr("The book's highlights, notes and bookmarks."));
    addCommand(QStringLiteral("practice"), tr("Practice Words"), QKeySequence(Qt::CTRL | Qt::Key_P), [this] { showPractice(); }, nullptr,
               tr("Flashcards of the words you looked up."));
    addCommand(QStringLiteral("bilingual"), tr("Show English Alongside"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B), [this] {
        m_reader->setBilingualVisible(!m_reader->bilingualVisible());
        syncChecks();
        saveSettingsSoon();
    }, [this] { return isReading(); }, tr("The English of every paragraph on the page, in a panel beside it (Gemini or LM Studio)."));
    command(QStringLiteral("bilingual"))->action->setCheckable(true);
    addCommand(QStringLiteral("statistics"), tr("Statistics"), QKeySequence(), [this] { showStatistics(); }, nullptr,
               tr("Time read, days in a row, books finished, words and reviews."));
    addCommand(QStringLiteral("word-list"), tr("Word List"), QKeySequence(), [this] { showPractice(true); }, nullptr,
               tr("Every word you looked up, with its meaning."));
    addCommand(QStringLiteral("translation-settings"), tr("Translation Settings…"), QKeySequence(),
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
    toggle("use-gemini", tr("Use Gemini (Online)"), &Settings::useGemini, tr("Google's Gemini, asked first."));
    toggle("use-lm-studio", tr("Use LM Studio (This Computer)"), &Settings::useLmStudio, tr("Asked when Gemini can't answer."));
    toggle("use-dictionary", tr("Use the Offline Dictionary"), &Settings::useDictionary, tr("Single words at once, and when nothing else answers."));
    toggle("use-offline-translator", tr("Use the Offline Translator"), &Settings::useOfflineTranslator,
           tr("Sentences and pages with no internet, when Gemini and LM Studio can't answer (a download in Settings)."));
    toggle("offline-only", tr("Offline Only"), &Settings::offlineOnly, tr("No text is sent over the internet: Gemini is never asked."));
    addCommand(QStringLiteral("keyboard-shortcuts"), tr("Keyboard Shortcuts"), QKeySequence(), [this] { showSettings(SettingsDialog::ShortcutsTab); },
               nullptr, tr("Every shortcut, and changing them."));
    applyShortcuts();
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
    auto watch = [this](QMenu* m) {
        connect(m, &QMenu::aboutToShow, this, [this] {
            for (auto& c : m_commands) c->action->setEnabled(c->isEnabled());
            syncChecks();
        });
        connect(m, &QMenu::aboutToHide, this, [this] {
            for (auto& c : m_commands) c->action->setEnabled(true);
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
    add(view, "book-offline");
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
    add(translation, "use-lm-studio");
    add(translation, "use-dictionary");
    add(translation, "use-offline-translator");
    translation->addSeparator();
    add(translation, "offline-only");
    add(translation, "book-offline");
    translation->addSeparator();
    add(translation, "translation-settings");
    watch(translation);
    m_themeMenu = settings->addMenu(tr("Theme"));
    m_themeMenu->setProperty("testId", "menuTheme");
    connect(m_themeMenu, &QMenu::aboutToShow, this, &MainWindow::fillThemeMenu);
    // Pointing at a theme previews it on the window; leaving the menu without choosing puts the saved one back.
    connect(m_themeMenu, &QMenu::aboutToHide, this, [this] { previewTheme(QString()); });
    settings->addSeparator();
    add(settings, "settings");
    add(settings, "data-folder");
    watch(settings);

    QMenu* help = bar->addMenu(tr("&Help"));
    help->setProperty("testId", "menuHelp");
    add(help, "keyboard-shortcuts");
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
    addTheme(m_themeMenu, QStringLiteral("system"), tr("Match Operating System"));
    m_themeMenu->addSeparator();
    for (const QString& g : Theme::groups()) {
        QMenu* sub = m_themeMenu->addMenu(g);
        connect(sub, &QMenu::aboutToHide, this, [this] { previewTheme(QString()); });
        for (const ThemeDef& d : themeTable())
            if (d.group == g) addTheme(sub, d.key, d.name);
    }
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
        file = QFileDialog::getOpenFileName(this, tr("Import Kindle Highlights"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                            tr("Kindle highlights (*.json *.txt);;All files (*)"));
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
        if (m_interactive) QMessageBox::information(this, tr("Import Kindle Highlights"), error);
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

QString MainWindow::dictionaryFile(const QString& language)
{
    return QDir(appdata::folder()).filePath(QStringLiteral("dictionaries/%1-en.sqlite").arg(language));
}

void MainWindow::applyTranslationSettings()
{
    m_dictionary->setEnabled(m_settings.useDictionary);
    if (!m_dictionary->info().ok) m_dictionary->open(dictionaryFile(QStringLiteral("es")));
    m_dictionaryEn->setEnabled(m_settings.useDictionary);
    if (!m_dictionaryEn->info().ok) m_dictionaryEn->open(dictionaryFile(QStringLiteral("en")));
    m_offline->setEnabled(m_settings.useOfflineTranslator);
    m_offline->setFolder(translatorFolder());
    m_gemini->setEnabled(m_settings.useGemini);
    m_gemini->setModel(m_settings.geminiModel);
    m_lmStudio->setEnabled(m_settings.useLmStudio);
    m_lmStudio->setAddress(m_settings.lmStudioAddress);
    m_lmStudio->setModel(m_settings.lmStudioModel);
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
    set("use-lm-studio", m_settings.useLmStudio);
    set("use-dictionary", m_settings.useDictionary);
    set("use-offline-translator", m_settings.useOfflineTranslator);
    set("offline-only", m_settings.offlineOnly);
    set("book-offline", m_reader && m_reader->bookIsOffline());
    set("bilingual", m_reader && m_reader->bilingualVisible());
}

void MainWindow::showSettings(int tab)
{
    if (!m_settingsDialog) {
        std::vector<Command*> list;
        for (auto& c : m_commands) list.push_back(c.get());
        m_settingsDialog = new SettingsDialog(m_settings, list, m_gemini, m_lmStudio, m_interactive, this);
        m_settingsDialog->setDictionary(m_dictionary, dictionaryFile(QStringLiteral("es")));
        m_settingsDialog->setEnglishDictionary(m_dictionaryEn, dictionaryFile(QStringLiteral("en")));
        m_settingsDialog->setOfflineTranslator(m_offline);
        connect(m_settingsDialog, &SettingsDialog::translationChanged, this, [this] {
            applyTranslationSettings();
            saveSettingsSoon();
        });
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
    }
    m_settingsDialog->setDataFolder(appdata::folder(), appdata::isTemporary());
    m_settingsDialog->refreshTranslation();
    m_settingsDialog->showTab(SettingsDialog::Tab(tab));
    m_settingsDialog->show();
    m_settingsDialog->raise();
}

Command* MainWindow::command(const QString& id) const
{
    for (const auto& c : m_commands)
        if (c->id == id) return c.get();
    return nullptr;
}

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
    m_reader->closeBook();
    saveSettingsNow();
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
        applyThemeTo(m_stack, t);
    } else {
        clearThemeFrom(m_stack);
        applyTheme(*qApp, t);
        m_theme = t;
    }
    m_reader->setTheme(t);
    m_start->setTheme(t);
    m_practice->setTheme(t);
    m_librarySearch->setTheme(t);
    m_statistics->setTheme(t);
    m_imageViewer->setTheme(t);
}

void MainWindow::showPractice(bool wordList)
{
    if (m_stack->currentWidget() != m_practice) m_beforePractice = m_stack->currentWidget();
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
        if (!m_stack->styleSheet().isEmpty()) {
            clearThemeFrom(m_stack);
            m_reader->setTheme(m_theme);
            m_start->setTheme(m_theme);
            m_imageViewer->setTheme(m_theme);
        }
        return;
    }
    showTheme(Theme::forChoice(key), true);
}

SettingsDialog* MainWindow::settingsDialog() const { return m_settingsDialog.data(); }

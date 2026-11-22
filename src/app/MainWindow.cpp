#include "MainWindow.h"

#include "AppData.h"
#include "Popups.h"
#include "ReaderView.h"
#include "StartView.h"
#include "ThemePicker.h"

#include "core/format/FormatPlugin.h"
#include "core/services/BookEngine.h"
#include "core/services/Library.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
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
    m_stack = new QStackedWidget;
    m_start = new StartView;
    m_reader = new ReaderView(m_engine, m_library, m_settings);
    m_reader->setInteractive(interactive);
    m_stack->addWidget(m_start);
    m_stack->addWidget(m_reader);
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
    connect(m_start, &StartView::openRequested, this, &MainWindow::chooseBook);
    connect(m_start, &StartView::bookRequested, this, &MainWindow::openBook);
    connect(m_start, &StartView::settingsLinkClicked, this, &MainWindow::changeDataFolder);
    connect(m_reader, &ReaderView::settingsLinkClicked, this, &MainWindow::changeDataFolder);
    connect(m_reader, &ReaderView::libraryRequested, this, &MainWindow::showStart);
    connect(m_reader, &ReaderView::commandsRequested, this, [this] { runCommand(QStringLiteral("commands")); });
    connect(m_reader, &ReaderView::fullScreenRequested, this, &MainWindow::toggleFullScreen);
    connect(m_reader, &ReaderView::imageRequested, this, [this](const QString& path) {
        QImage img;
        img.loadFromData(m_engine->resource(path));
        m_imageViewer->showImage(img);
    });

    m_start->setSettingsNotKept(appdata::isTemporary());
    m_reader->setSettingsNotKept(appdata::isTemporary());
    if (m_library) m_start->setRecent(m_library->recentBooks(8));

    buildCommands();
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
    // Shortcut overrides from the settings ("id=Ctrl+X").
    for (const QString& o : m_settings.shortcuts)
        if (o.section(QLatin1Char('='), 0, 0) == id) c->shortcut = QKeySequence(o.section(QLatin1Char('='), 1), QKeySequence::PortableText);
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
    addCommand(QStringLiteral("start"), tr("Go to Start Screen"), QKeySequence(Qt::CTRL | Qt::Key_L), [this] { showStart(); }, reading);
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
    addCommand(QStringLiteral("larger"), tr("Larger Text"), QKeySequence(Qt::CTRL | Qt::Key_Equal), [this] { changeTextSize(10); });
    addCommand(QStringLiteral("smaller"), tr("Smaller Text"), QKeySequence(Qt::CTRL | Qt::Key_Minus), [this] { changeTextSize(-10); });
    addCommand(QStringLiteral("default-size"), tr("Default Text Size"), QKeySequence(Qt::CTRL | Qt::Key_0), [this] { changeTextSize(0); });
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
    // Second shortcuts people expect for the palette.
    if (Command* c = command(QStringLiteral("commands")))
        c->action->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_K), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), QKeySequence(Qt::Key_F1)});
    if (Command* c = command(QStringLiteral("larger")))
        c->action->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Plus)});
    if (Command* c = command(QStringLiteral("back")))
        c->action->setShortcuts({QKeySequence(Qt::ALT | Qt::Key_Left), QKeySequence(Qt::Key_Backspace)});
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
    if (!m_openingPath.isEmpty()) return false;
    return m_stack->currentWidget() == m_start || m_reader->isSettled();
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
    emit bookShown();
}

void MainWindow::onOpenFailed(const QString& message)
{
    const QString name = QFileInfo(m_openingPath).fileName();
    m_openingPath.clear();
    m_lastError = message;
    m_stack->setCurrentWidget(m_start);
    m_start->setMessage(tr("Couldn't open %1. %2").arg(name, message));
    emit bookFailed(message);
}

void MainWindow::showStart()
{
    m_reader->closeBook();
    m_engine->close();
    setWindowTitle(QStringLiteral(APP_NAME));
    if (m_library) m_start->setRecent(m_library->recentBooks(8));
    m_stack->setCurrentWidget(m_start);
}

void MainWindow::changeTextSize(int delta)
{
    const int now = m_reader->effectiveAppearance().textSizePct;
    m_reader->changeAppearance(QStringLiteral("text_size_pct"), delta == 0 ? 100 : qBound(60, now + delta, 300));
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
    m_imageViewer->setTheme(t);
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
            m_imageViewer->setTheme(m_theme);
        }
        return;
    }
    showTheme(Theme::forChoice(key), true);
}
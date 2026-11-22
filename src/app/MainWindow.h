// The main window: the start screen and the reader, the command palette (instead of a menu bar), opening books
// and the shortcuts.
#pragma once

#include "Commands.h"
#include "Settings.h"
#include "Theme.h"

#include "core/position/Position.h"

#include <QMainWindow>

#include <memory>
#include <vector>

class CommandPalette;
class ImageViewer;
class QStackedWidget;
class ReaderView;
class StartView;

namespace core {
class BookEngine;
class Library;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // `interactive` = false for the screenshot mode and tests: never a dialog, never the user's browser.
    MainWindow(Settings& settings, core::Library* library, bool interactive = true, QWidget* parent = nullptr);
    ~MainWindow() override;

    void openBook(const QString& path);
    bool isSettled() const;              // a book is shown and ready, or the start screen is shown
    bool isReading() const;
    QString lastError() const { return m_lastError; }

    ReaderView* reader() const { return m_reader; }
    StartView* start() const { return m_start; }
    CommandPalette* palette() const { return m_palette; }
    core::BookEngine* engine() const { return m_engine; }
    Command* command(const QString& id) const;
    void runCommand(const QString& id);
    void saveSettingsSoon();
    void saveSettingsNow();

signals:
    void bookShown();
    void bookFailed(const QString& message);

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void buildCommands();
    void addCommand(const QString& id, const QString& label, const QKeySequence& key, std::function<void()> run,
                    std::function<bool()> enabled = nullptr, const QString& hint = QString());
    void showStart();
    void chooseBook();
    void onOpened();
    void onOpenFailed(const QString& message);
    void changeTextSize(int delta);
    void setColumns(const QString& mode);
    void toggleFullScreen();
    void changeDataFolder();
    void about();

    Settings& m_settings;
    core::Library* m_library;
    bool m_interactive;
    Theme m_theme = Theme::midnight();
    core::BookEngine* m_engine;
    QStackedWidget* m_stack;
    StartView* m_start;
    ReaderView* m_reader;
    CommandPalette* m_palette;
    ImageViewer* m_imageViewer;
    std::vector<std::unique_ptr<Command>> m_commands;
    QString m_openingPath;
    QString m_lastError;
    QTimer* m_settingsTimer;
    bool m_wasMaximized = false;
};

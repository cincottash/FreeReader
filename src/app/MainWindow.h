// The main window: the menu bar, the library and the reader, opening books and the shortcuts. Every action is a
// command (label, shortcut that can be changed in Settings); the menus and the keyboard-only palette (Ctrl+K) list them.
#pragma once

#include "Commands.h"
#include "Settings.h"
#include "Theme.h"

#include "core/position/Position.h"

#include <QMainWindow>
#include <QPointer>

#include <memory>
#include <vector>

class CommandPalette;
class SettingsDialog;
class ThemePicker;
class ImageViewer;
class QMenu;
class QNetworkAccessManager;
class QStackedWidget;
class ReaderView;
class LibraryView;
class PracticeView;
class LibrarySearchView;
class StatisticsView;

namespace core {
class BookEngine;
class Library;
class GeminiProvider;
class LmStudioProvider;
class TranslationService;
class WordLog;
class OfflineDictionary;
class OfflineTranslator;
class SavedWords;
class Annotations;
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
    LibraryView* library() const { return m_start; }
    PracticeView* practice() const { return m_practice; }
    LibrarySearchView* librarySearch() const { return m_librarySearch; }
    StatisticsView* statistics() const { return m_statistics; }
    void showStatistics();
    void showLibrarySearch(const QString& query = QString());
    core::WordLog* wordLog() const { return m_words.get(); }
    core::Annotations* annotations() const { return m_annotations.get(); }
    void showPractice(bool wordList = false);
    CommandPalette* palette() const { return m_palette; }
    core::BookEngine* engine() const { return m_engine; }
    Command* command(const QString& id) const;
    void runCommand(const QString& id);
    void saveSettingsSoon();
    void saveSettingsNow();
    void chooseTheme(const QString& key);       // saves it, restyles the whole app
    void previewTheme(const QString& key);      // empty: the saved theme again
    ThemePicker* themePicker() const { return m_themePicker; }
    const Theme& theme() const { return m_theme; }

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
    void buildMenus();
    void fillThemeMenu();
    void addCommand(const QString& id, const QString& label, const QKeySequence& key, std::function<void()> run,
                    std::function<bool()> enabled = nullptr, const QString& hint = QString());
    void showLibrary();
    void chooseBook();
    void onOpened();
    void onOpenFailed(const QString& message);
    void changeTextSize(int delta);
    void setColumns(const QString& mode);
    void toggleFullScreen();
    void changeDataFolder();
    void about();
    void showTheme(const Theme& t, bool preview);
    void applyShortcuts();
    void applyPendingJump();

public:
    void showSettings(int tab = 0);
    void applyTranslationSettings();
    void translationOptionsChanged();   // a switch in the menu changed: apply, save, update the open dialog
    void syncChecks();                  // check marks of the menu switches from the settings
    core::TranslationService* translator() const { return m_translator; }
    core::GeminiProvider* gemini() const { return m_gemini; }
    core::LmStudioProvider* lmStudio() const { return m_lmStudio; }
    core::OfflineDictionary* dictionary() const { return m_dictionary; }
    core::OfflineDictionary* englishDictionary() const { return m_dictionaryEn; }
    static QString dictionaryFile(const QString& language);   // <data folder>/dictionaries/<language>-en.sqlite
    static QString translatorFolder();   // <data folder>/translator: the program and one folder per language
    core::OfflineTranslator* offlineTranslator() const { return m_offline; }
    core::SavedWords* savedWords() const { return m_savedWords; }
    SettingsDialog* settingsDialog() const;
    // File > Import Kindle Highlights…: ile empty asks for one. The dialog stays open with the result.
    void importKindle(const QString& file = QString());
    class KindleImportDialog* kindleDialog() const;

private:

    Settings& m_settings;
    core::Library* m_library;
    bool m_interactive;
    Theme m_theme = Theme::midnight();
    core::BookEngine* m_engine;
    QStackedWidget* m_stack;
    LibraryView* m_start;   // the library
    ReaderView* m_reader;
    PracticeView* m_practice;
    std::unique_ptr<core::WordLog> m_words;
    std::unique_ptr<core::Annotations> m_annotations;
    QWidget* m_beforePractice = nullptr;
    LibrarySearchView* m_librarySearch = nullptr;
    QWidget* m_beforeLibrarySearch = nullptr;
    StatisticsView* m_statistics = nullptr;
    QWidget* m_beforeStatistics = nullptr;
    // Show in Book from the word list: where to go once the book is open.
    struct PendingJump { QString cfi; int spine = -1, start = 0, end = 0; } m_pendingJump;
    CommandPalette* m_palette;
    ImageViewer* m_imageViewer;
    ThemePicker* m_themePicker;
    QString m_previewKey;
    QPointer<SettingsDialog> m_settingsDialog;
    QPointer<class KindleImportDialog> m_kindleDialog;
    QMenu* m_themeMenu = nullptr;
    QNetworkAccessManager* m_net;
    core::GeminiProvider* m_gemini;
    core::LmStudioProvider* m_lmStudio;
    core::TranslationService* m_translator;
    core::OfflineDictionary* m_dictionary;
    core::OfflineDictionary* m_dictionaryEn;
    core::SavedWords* m_savedWords = nullptr;
    core::OfflineTranslator* m_offline = nullptr;
    std::vector<std::unique_ptr<Command>> m_commands;
    QString m_openingPath;
    QString m_lastError;
    QTimer* m_settingsTimer;
    bool m_wasMaximized = false;
};

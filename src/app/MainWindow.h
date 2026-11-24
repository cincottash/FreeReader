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
class HelpPanel;
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
    PracticeView* practiceSide() const { return m_practiceSide; }
    // Help > Getting Started: the guide in a panel on the window's right, in the library and while reading.
    HelpPanel* helpPanel() const { return m_help; }
    // The welcome on the first start: the reader's language and the language packs to download.
    void showWelcome();
    class WelcomeDialog* welcome() const;
    // Downloads these languages' packs in Settings > General, where their progress shows.
    void downloadPacks(const QStringList& languages);
    // The file of the book open now (empty in the library), for reopening it in a rebuilt window.
    QString currentBookPath() const;
    // Before the window is replaced by one in another language: the book's place and the settings saved.
    void prepareToClose();
    // The language this window's text was built in (its menus and labels don't change after).
    QString builtLanguage() const { return m_builtLanguage; }
    bool helpVisible() const;
    void setHelpVisible(bool on);
    bool helpOverlays() const;   // over the screen's right side (no room beside it), not beside it
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
    // The reader chose another language for the app's text (the welcome, or Settings > General > Restart Now): the
    // window is rebuilt in it (main.cpp), the book reopened and the packs asked for downloaded.
    void interfaceLanguageChanged(const QString& bookPath, const QStringList& packs);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;   // mouse-button shortcuts
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
    void endPreview();   // the screen a preview restyled goes back to the app's theme
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
    core::OfflineDictionary* spanishDefinitions() const { return m_definitionsEs; }
    core::OfflineDictionary* englishSpanishDictionary() const { return m_dictionaryEnEs; }
    // <data folder>/dictionaries/: "es" es-en.sqlite, "en" en-en.sqlite, "es-es" es-es.sqlite, "en-es" en-es.sqlite
    static QString dictionaryFile(const QString& key);
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
    PracticeView* m_practiceSide;   // the practice workspace beside the page (in the reader)
    HelpPanel* m_help = nullptr;
    QWidget* m_body = nullptr;            // the central widget: the screens and the guide
    class QHBoxLayout* m_bodyLay = nullptr;
    void placeHelp();
    QPointer<class WelcomeDialog> m_welcome;
    void ensureSettingsDialog();
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
    QPointer<QWidget> m_previewTarget;   // the screen a theme preview is shown on
    QPointer<class KindleImportDialog> m_kindleDialog;
    QMenu* m_themeMenu = nullptr;
    QNetworkAccessManager* m_net;
    core::GeminiProvider* m_gemini;
    core::LmStudioProvider* m_lmStudio;
    core::TranslationService* m_translator;
    core::OfflineDictionary* m_dictionary;
    core::OfflineDictionary* m_dictionaryEn;
    core::OfflineDictionary* m_definitionsEs;
    core::OfflineDictionary* m_dictionaryEnEs;   // English -> Spanish, for Spanish readers
    QString m_builtLanguage;
    QStringList m_welcomePacks;                  // the packs the welcome asked for, until it has closed
    core::SavedWords* m_savedWords = nullptr;
    core::OfflineTranslator* m_offline = nullptr;
    std::vector<std::unique_ptr<Command>> m_commands;
    QString m_openingPath;
    QString m_lastError;
    QTimer* m_settingsTimer;
    bool m_wasMaximized = false;
};

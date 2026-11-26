// Settings, in three tabs:
// - Translation: the Gemini key (kept in the system's credential store, never in a file) and model, the books kept
//   offline (Gemini isn't asked about them), LM Studio's address, and whether selecting text opens the pop-up.
//   Test and Check try them out. This tab is the one place for every translation option; Settings > Translation in
//   the menu bar has the quick switches.
// - Keyboard Shortcuts: every command, editable; a key can't be used twice.
// - General: the app data folder, the library's folders, and resetting text and layout.
// Changes apply at once so they can be seen; OK keeps them, Cancel (also Escape and the window's close button)
// puts back what the window changed since it opened (begin()).
#pragma once

#include "Commands.h"
#include "core/translate/AiProviders.h"
#include "core/translate/DictionaryDownload.h"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QLocale>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTreeWidget;
struct Settings;

namespace core {
class GeminiProvider;
class LmStudioProvider;
class OfflineDictionary;
class OfflineTranslator;
struct DictionaryInfo;
}

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    enum Tab { GeneralTab = 0, TranslationTab = 1, AudioTab = 2, ShortcutsTab = 3 };

    SettingsDialog(Settings& settings, const std::vector<Command*>& commands, core::GeminiProvider* gemini,
                   core::LmStudioProvider* lmStudio, bool interactive, QWidget* parent);
    void showTab(Tab tab);
    void begin();   // remembers the settings as they are: Cancel goes back to them
    void accept() override;
    void reject() override;

    // Sets a command's shortcut; returns a message when the keys are taken (then nothing changes).
    QString setShortcut(const QString& id, const QKeySequence& keys);
    // A mouse button for a command ("Back", "Ctrl+Forward"; empty clears it). A message when another command has it.
    QString setMouseShortcut(const QString& id, const QString& binding);
    void resetShortcuts();
    QTreeWidget* shortcutList() const { return m_list; }
    void setDataFolder(const QString& folder, bool temporary);

    // Translation tab (also used by tests)
    QString saveGeminiKey(const QString& key);   // empty when saved, else what went wrong
    void removeGeminiKey();
    void testGemini();
    void checkLmStudio();
    void testLmStudio();
    void refreshTranslation();
    // Keep This Book Offline: `openBook` gives the book open now (its id, 0 for none, and its title).
    void setOpenBook(std::function<std::pair<int, QString>()> openBook) { m_openBook = std::move(openBook); }
    void refreshOfflineBooks();   // after the open book or the list of offline books changed
    void setOpenBookOffline(bool on);
    void setDictionary(core::OfflineDictionary* dictionary, const QString& installPath);
    void setEnglishDictionary(core::OfflineDictionary* dictionary, const QString& installPath);
    // `key`: "es" (Spanish -> English), "en" (English definitions) or "es-es" (Spanish definitions).
    void setDictionaryFor(const QString& key, core::OfflineDictionary* dictionary, const QString& installPath);
    static QString dictionaryKey(const core::DictionaryInfo& info);   // empty: not one of these
    // Installs a dictionary file (Spanish -> English or English definitions, as the file says). Empty when
    // installed, else why not.
    QString installDictionary(const QString& file);
    // `language`: the dictionary's key ("es", "en" or "es-es", as above).
    void removeDictionary(const QString& language = QStringLiteral("es"));
    // Download: from FreeReader's GitHub releases (tests point it at a local server).
    void downloadDictionary(const QString& language = QStringLiteral("es"));
    void cancelDictionaryDownload(const QString& language = QStringLiteral("es"));
    bool isDownloadingDictionary(const QString& language = QString()) const;   // empty: either
    void setDictionarySource(const core::DictionaryDownload::Source& s, const QString& language = QStringLiteral("es"));
    // The offline sentence translator: a pack (the program and its Spanish -> English model) downloaded from
    // FreeReader's GitHub releases into the translator's folder.
    void setOfflineTranslator(core::OfflineTranslator* translator);
    void downloadTranslator();
    void cancelTranslatorDownload();
    void removeTranslator();
    bool isDownloadingTranslator() const;
    void setTranslatorSource(const core::DictionaryDownload::Source& s);
    // One more model for the translator ("en-es"), unpacked into its own folder beside the program's.
    void downloadTranslatorModel(const QString& pair);
    void cancelTranslatorModelDownload();
    bool isDownloadingTranslatorModel() const;
    void removeTranslatorModel(const QString& pair);
    void setTranslatorModelSource(const QString& pair, const core::DictionaryDownload::Source& s);   // tests
    QString lastMessage() const;
    // General tab, Languages: the reader's own language, and a pack per language read (its dictionary, its
    // definitions and the offline translator), downloaded part after part with one button.
    void setReaderLanguage(const QString& code);
    void downloadPack(const QString& language);
    void downloadPacks(const QStringList& languages);   // one after another (the welcome's choice)
    void cancelPackDownload();
    void removePack(const QString& language);
    bool isPackInstalled(const QString& language) const;
    bool isDownloadingPack() const { return !m_packLanguage.isEmpty(); }
    QString downloadingPack() const { return m_packLanguage; }
    // The language the app's text is in now: another one chosen shows Restart Now (the window is rebuilt in it).
    void setInterfaceLanguage(const QString& code);

    // Audio tab: the voices installed (by language) and a way to hear one; MainWindow gives both.
    struct Voice {
        QString name;
        QLocale locale;
    };
    void setVoiceSource(std::function<QList<Voice>()> voices, std::function<QString(const QString&)> playSample)
    {
        m_voiceSource = std::move(voices);
        m_playSample = std::move(playSample);
    }
    void fillVoices();   // lists the voices again (when the Audio tab is shown)
    void setSpeechRate(int pct);
    void setSpeechVolume(int pct);
    void chooseVoice(const QString& language, const QString& voice);   // empty voice: the system's choice
    QString playSample(const QString& language);
    // The library's folders (checked for new books at every start).
    void fillLibraryFolders();
    void removeLibraryFolder(const QString& folder);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;   // the Mouse cells waiting for a button

signals:
    void changeDataFolderRequested();
    void shortcutsChanged();
    void resetTextAndLayoutRequested();
    void translationChanged();   // a translation option changed: apply it and save the settings
    void audioChanged();         // the speed, volume or a voice changed: save the settings
    void languageChanged();      // the reader's language or the languages read changed: apply and save
    void packFinished(const QString& language, const QString& error);   // error empty: the whole pack is installed
    void restartRequested();     // Restart Now: the window again, in the language chosen
    void libraryFoldersChanged();
    void addLibraryFolderRequested();
    void cancelled();   // the settings were put back: apply them again
    void pageTurnChanged();   // the page turn animation, its duration or motion, or the arrows changed: apply and save

private:
    QWidget* buildTranslationTab();
    QWidget* buildShortcutsTab();
    QWidget* buildGeneralTab();
    QWidget* buildAudioTab();
    void fillAudio();
    void fill();
    void fillPageTurns();   // Settings > General > Page Turns, from the settings
    QComboBox* m_turnStyle = nullptr;
    class QSlider* m_turnSpeed = nullptr;
    QLabel* m_turnSpeedValue = nullptr;
    QComboBox* m_turnMotion = nullptr;
    QCheckBox* m_turnArrows = nullptr;
    void fillLmModels(const QList<core::LmStudioProvider::Model>& models);
    static core::TranslationRequest testRequest();   // the word Test looks up

    Settings& m_settings;
    std::vector<Command*> m_commands;
    core::GeminiProvider* m_gemini;
    core::LmStudioProvider* m_lm;
    bool m_interactive;
    QTabWidget* m_tabs;

    QTreeWidget* m_list;
    QLabel* m_message;
    QLabel* m_folder;
    class QListWidget* m_libraryFolders;
    QPushButton* m_removeFolder = nullptr;
    std::shared_ptr<Settings> m_before;   // as they were when the window opened
    bool m_lookReset = false;

    QLabel* m_keyStatus;
    QLineEdit* m_keyEdit;
    QPushButton* m_removeKey;
    QComboBox* m_model;
    QCheckBox* m_useGemini;
    QLabel* m_geminiResult;
    QCheckBox* m_useLm;
    QLineEdit* m_lmAddress;
    QComboBox* m_lmModel;
    class QDoubleSpinBox* m_geminiWait = nullptr;   // how long a lookup waits for Gemini
    class QDoubleSpinBox* m_lmWait = nullptr;       // and for LM Studio
    QLabel* m_lmResult;
    QCheckBox* m_bookOffline;
    QLabel* m_offlineCount;
    std::function<std::pair<int, QString>()> m_openBook;
    struct DictRow {
        // Its key: "es" Spanish -> English, "en" English definitions, "es-es" Spanish definitions, "en-es" English ->
        // Spanish.
        QString language;
        core::OfflineDictionary* dict = nullptr;
        QString path;
        core::DictionaryDownload::Source source;
        QLabel* status = nullptr;
        QPushButton* download = nullptr;
        QPushButton* remove = nullptr;
        class QProgressBar* progress = nullptr;
        core::DictionaryDownload* downloader = nullptr;
    };
    DictRow m_rows[4];
    DictRow* row(const QString& language);
    const DictRow* row(const QString& language) const;
    void installDownloaded(const QString& language, const QString& file, const QString& error);
    QCheckBox* m_useDict;
    QCheckBox* m_auto;

    struct PackRow {
        QString language;
        QLabel* status = nullptr;
        QPushButton* get = nullptr;
        QPushButton* remove = nullptr;
        class QProgressBar* progress = nullptr;
    };
    std::vector<PackRow> m_packRows;
    QWidget* m_packBox = nullptr;
    QComboBox* m_readerLanguage = nullptr;
    QString m_interfaceLanguage = QStringLiteral("en");
    QWidget* m_restartRow = nullptr;
    QLabel* m_restartNote = nullptr;
    void refreshRestart();
    QString m_packLanguage;            // the pack downloading now
    QStringList m_packQueue;           // its parts still to get (keys), then the next packs (as "pack:<language>")
    qint64 m_packDone = 0;             // bytes of its parts already downloaded
    QString m_packPart;                // the part downloading now
    void buildPackRows();
    void refreshPacks();
    void startNextPackPart();
    void packPartDone(const QString& key);
    void packProgress(qint64 got);
    bool partInstalled(const QString& key) const;

    std::function<QList<Voice>()> m_voiceSource;
    std::function<QString(const QString&)> m_playSample;
    class QSlider* m_rate = nullptr;
    class QSlider* m_volume = nullptr;
    QLabel* m_rateLabel = nullptr;
    QLabel* m_volumeLabel = nullptr;
    QWidget* m_voiceRows = nullptr;
    QLabel* m_voiceNote = nullptr;
    QLabel* m_sampleResult = nullptr;

    core::OfflineTranslator* m_translatorEngine = nullptr;
    core::DictionaryDownload::Source m_translatorSource;
    core::DictionaryDownload* m_translatorDownload = nullptr;
    QCheckBox* m_useTranslator;
    QLabel* m_translatorStatus;
    QPushButton* m_translatorGet;
    QPushButton* m_translatorRemove;
    class QProgressBar* m_translatorProgress;
    void installTranslator(const QString& folder, const QString& error);
    core::DictionaryDownload* m_modelDownload = nullptr;
    QString m_modelPair;               // the model downloading now
    QHash<QString, core::DictionaryDownload::Source> m_modelSources;
    void installTranslatorModel(const QString& pair, const QString& unpacked, const QString& error);
};

// Settings, in three tabs:
// - Translation: the Gemini key (kept in the system's credential store, never in a file) and model, LM Studio's
//   address, offline only, and whether selecting text opens the pop-up. Test and Check try them out.
// - Keyboard Shortcuts: every command, editable; a key can't be used twice.
// - General: the app data folder and the resets.
// Changes apply at once; there is no OK or Cancel.
#pragma once

#include "Commands.h"
#include "core/translate/AiProviders.h"
#include "core/translate/DictionaryDownload.h"

#include <QDialog>

#include <functional>
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
    enum Tab { TranslationTab = 0, ShortcutsTab = 1, GeneralTab = 2 };

    SettingsDialog(Settings& settings, const std::vector<Command*>& commands, core::GeminiProvider* gemini,
                   core::LmStudioProvider* lmStudio, bool interactive, QWidget* parent);
    void showTab(Tab tab);

    // Sets a command's shortcut; returns a message when the keys are taken (then nothing changes).
    QString setShortcut(const QString& id, const QKeySequence& keys);
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
    QString lastMessage() const;

signals:
    void changeDataFolderRequested();
    void shortcutsChanged();
    void resetTextAndLayoutRequested();
    void translationChanged();   // a translation option changed: apply it and save the settings

private:
    QWidget* buildTranslationTab();
    QWidget* buildShortcutsTab();
    QWidget* buildGeneralTab();
    void fill();
    void fillLmModels(const QList<core::LmStudioProvider::Model>& models);

    Settings& m_settings;
    std::vector<Command*> m_commands;
    core::GeminiProvider* m_gemini;
    core::LmStudioProvider* m_lm;
    bool m_interactive;
    QTabWidget* m_tabs;

    QTreeWidget* m_list;
    QLabel* m_message;
    QLabel* m_folder;

    QLabel* m_keyStatus;
    QLineEdit* m_keyEdit;
    QPushButton* m_removeKey;
    QComboBox* m_model;
    QCheckBox* m_useGemini;
    QLabel* m_geminiResult;
    QCheckBox* m_useLm;
    QLineEdit* m_lmAddress;
    QComboBox* m_lmModel;
    QLabel* m_lmResult;
    QCheckBox* m_offline;
    struct DictRow {
        QString language;   // its key: "es" Spanish -> English, "en" English definitions, "es-es" Spanish definitions
        core::OfflineDictionary* dict = nullptr;
        QString path;
        core::DictionaryDownload::Source source;
        QLabel* status = nullptr;
        QPushButton* download = nullptr;
        QPushButton* add = nullptr;
        QPushButton* remove = nullptr;
        class QProgressBar* progress = nullptr;
        core::DictionaryDownload* downloader = nullptr;
    };
    DictRow m_rows[3];
    DictRow* row(const QString& language);
    const DictRow* row(const QString& language) const;
    void installDownloaded(const QString& language, const QString& file, const QString& error);
    QCheckBox* m_useDict;
    QCheckBox* m_auto;

    core::OfflineTranslator* m_translatorEngine = nullptr;
    core::DictionaryDownload::Source m_translatorSource;
    core::DictionaryDownload* m_translatorDownload = nullptr;
    QCheckBox* m_useTranslator;
    QLabel* m_translatorStatus;
    QPushButton* m_translatorGet;
    QPushButton* m_translatorRemove;
    class QProgressBar* m_translatorProgress;
    void installTranslator(const QString& folder, const QString& error);
};

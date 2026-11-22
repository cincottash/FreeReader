// Settings, in three tabs:
// - Translation: the Gemini key (kept in the system's credential store, never in a file) and model, LM Studio's
//   address, offline only, and whether selecting text opens the pop-up. Test and Check try them out.
// - Keyboard Shortcuts: every command, editable; a key can't be used twice.
// - General: the app data folder and the resets.
// Changes apply at once; there is no OK or Cancel.
#pragma once

#include "Commands.h"

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
    void refreshTranslation();

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
    QLabel* m_lmResult;
    QCheckBox* m_offline;
    QCheckBox* m_auto;
};

// Settings: the app data folder, keyboard shortcuts (every command, editable; a key can't be used twice), and
// separate resets for text and layout and for shortcuts. Changes apply at once; there is no OK or Cancel.
#pragma once

#include "Commands.h"

#include <QDialog>

#include <functional>
#include <vector>

class QLabel;
class QTreeWidget;
struct Settings;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Settings& settings, const std::vector<Command*>& commands, QWidget* parent);
    // Sets a command's shortcut; returns a message when the keys are taken (then nothing changes).
    QString setShortcut(const QString& id, const QKeySequence& keys);
    void resetShortcuts();
    QTreeWidget* shortcutList() const { return m_list; }
    void setDataFolder(const QString& folder, bool temporary);

signals:
    void changeDataFolderRequested();
    void shortcutsChanged();
    void resetTextAndLayoutRequested();

private:
    void fill();

    Settings& m_settings;
    std::vector<Command*> m_commands;
    QTreeWidget* m_list;
    QLabel* m_message;
    QLabel* m_folder;
};

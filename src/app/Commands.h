// Commands: every action of the app, with its label and shortcut. The command palette (Ctrl+K) lists them
// instead of a classic menu bar; shortcuts can be overridden in the settings ("id=Ctrl+X").
#pragma once

#include <QFrame>
#include <QKeySequence>
#include <QString>

#include <functional>
#include <vector>

class QAction;
class QLabel;
class QLineEdit;
class QListWidget;

struct Command {
    QString id;
    QString label;          // Title Case
    QString hint;           // sentence shown under the label in the palette (optional)
    QKeySequence shortcut;          // in effect (the default, or the one set in Settings)
    QKeySequence defaultShortcut;
    std::function<void()> run;
    std::function<bool()> enabled;   // null = always
    QAction* action = nullptr;
    bool isEnabled() const { return !enabled || enabled(); }
};

class CommandPalette : public QFrame {
    Q_OBJECT
public:
    explicit CommandPalette(QWidget* parent);
    void setCommands(const std::vector<Command*>& commands) { m_commands = commands; }
    void showCommands();
    // A one-line question (Go to Page). `accept` returns an error message, or empty when done.
    void showPrompt(const QString& title, const QString& placeholder, std::function<QString(const QString&)> accept);
    void place();

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void refill();
    void runCurrent();

    QLabel* m_title;
    QLineEdit* m_edit;
    QListWidget* m_list;
    QLabel* m_error;
    std::vector<Command*> m_commands;
    std::function<QString(const QString&)> m_prompt;
};

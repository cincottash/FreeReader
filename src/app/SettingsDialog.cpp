#include "SettingsDialog.h"

#include "Settings.h"

#include <QAction>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QLabel* sectionTitle(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("PanelTitle"));
    l->setStyleSheet(QStringLiteral("padding: 10px 0 2px 0;"));
    return l;
}

}  // namespace

SettingsDialog::SettingsDialog(Settings& settings, const std::vector<Command*>& commands, QWidget* parent)
    : QDialog(parent), m_settings(settings), m_commands(commands)
{
    setWindowTitle(tr("Settings"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 12, 20, 16);
    lay->setSpacing(8);

    lay->addWidget(sectionTitle(tr("APP DATA FOLDER")));
    auto* folderRow = new QHBoxLayout;
    m_folder = new QLabel;
    m_folder->setWordWrap(true);
    m_folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_folder->setProperty("testId", "dataFolder");
    folderRow->addWidget(m_folder, 1);
    auto* change = new QPushButton(tr("Change…"));
    change->setToolTip(tr("Where the library database (reading places, collections) and the settings are kept. Books stay where they are."));
    folderRow->addWidget(change);
    lay->addLayout(folderRow);
    connect(change, &QPushButton::clicked, this, &SettingsDialog::changeDataFolderRequested);

    lay->addWidget(sectionTitle(tr("KEYBOARD SHORTCUTS")));
    auto* hint = new QLabel(tr("Click a shortcut and press the new keys. Backspace clears it."));
    hint->setObjectName(QStringLiteral("Muted"));
    lay->addWidget(hint);
    m_list = new QTreeWidget;
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({tr("Command"), tr("Shortcut")});
    m_list->setRootIsDecorated(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_list->header()->resizeSection(1, 200);
    m_list->header()->setStretchLastSection(false);
    m_list->setMinimumHeight(320);
    m_list->setProperty("testId", "shortcuts");
    lay->addWidget(m_list, 1);
    m_message = new QLabel;
    m_message->setWordWrap(true);
    m_message->setProperty("testId", "settingsMessage");
    m_message->hide();
    lay->addWidget(m_message);

    auto* buttons = new QHBoxLayout;
    auto* resetLook = new QPushButton(tr("Reset Text and Layout"));
    resetLook->setToolTip(tr("Font, size, spacing, margins, page width and columns for all books go back to the defaults. "
                             "Settings made for a language or a single book stay."));
    resetLook->setProperty("testId", "resetLook");
    auto* resetKeys = new QPushButton(tr("Reset Shortcuts"));
    resetKeys->setToolTip(tr("Every command gets its original keys back."));
    resetKeys->setProperty("testId", "resetShortcuts");
    auto* close = new QPushButton(tr("Close"));
    close->setDefault(true);
    buttons->addWidget(resetLook);
    buttons->addWidget(resetKeys);
    buttons->addStretch(1);
    buttons->addWidget(close);
    lay->addLayout(buttons);
    connect(resetLook, &QPushButton::clicked, this, [this] {
        emit resetTextAndLayoutRequested();
        m_message->setText(tr("Text and layout for all books are back to the defaults."));
        m_message->show();
    });
    connect(resetKeys, &QPushButton::clicked, this, &SettingsDialog::resetShortcuts);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    resize(620, 640);
    fill();
}

void SettingsDialog::setDataFolder(const QString& folder, bool temporary)
{
    m_folder->setText(temporary ? tr("Not kept: a temporary folder that is deleted when the app closes.") : QDir::toNativeSeparators(folder));
}

void SettingsDialog::fill()
{
    m_list->clear();
    for (Command* c : m_commands) {
        auto* it = new QTreeWidgetItem(m_list, {c->label});
        it->setToolTip(0, c->hint);
        it->setData(0, Qt::UserRole, c->id);
        auto* edit = new QKeySequenceEdit(c->action->shortcut());
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        edit->setProperty("commandId", c->id);
        m_list->setItemWidget(it, 1, edit);
        connect(edit, &QKeySequenceEdit::editingFinished, this, [this, edit, id = c->id] {
            const QString err = setShortcut(id, edit->keySequence());
            if (!err.isEmpty()) {
                for (Command* c2 : m_commands)
                    if (c2->id == id) edit->setKeySequence(c2->action->shortcut());
            }
        });
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [this, edit, id = c->id](const QKeySequence& k) {
            if (k.isEmpty()) setShortcut(id, k);   // the clear button
            (void)edit;
        });
    }
}

QString SettingsDialog::setShortcut(const QString& id, const QKeySequence& keys)
{
    Command* target = nullptr;
    for (Command* c : m_commands)
        if (c->id == id) target = c;
    if (!target) return {};
    if (!keys.isEmpty()) {
        for (Command* c : m_commands) {
            if (c == target) continue;
            if (c->action->shortcuts().contains(keys)) {
                const QString msg = tr("%1 is already used by %2. Change that one first.").arg(keys.toString(QKeySequence::NativeText), c->label);
                m_message->setText(msg);
                m_message->show();
                return msg;
            }
        }
    }
    QStringList kept;
    for (const QString& o : m_settings.shortcuts)
        if (o.section(QLatin1Char('='), 0, 0) != id) kept << o;
    if (keys != target->defaultShortcut) kept << id + QLatin1Char('=') + keys.toString(QKeySequence::PortableText);
    m_settings.shortcuts = kept;
    m_message->hide();
    emit shortcutsChanged();
    return {};
}

void SettingsDialog::resetShortcuts()
{
    m_settings.shortcuts.clear();
    emit shortcutsChanged();
    fill();
    m_message->setText(tr("Every command has its original keys again."));
    m_message->show();
}

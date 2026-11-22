#include "SettingsDialog.h"

#include "Settings.h"

#include "core/services/Secrets.h"
#include "core/translate/AiProviders.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const QString kGeminiKey = QStringLiteral("gemini-api-key");

QLabel* sectionTitle(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("PanelTitle"));
    l->setStyleSheet(QStringLiteral("padding: 12px 0 2px 0;"));
    return l;
}

QLabel* muted(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("Muted"));
    l->setWordWrap(true);
    return l;
}

QString storeName()
{
#ifdef Q_OS_WIN
    return QObject::tr("Windows Credential Manager");
#else
    return QObject::tr("the system's key store");
#endif
}

}  // namespace

SettingsDialog::SettingsDialog(Settings& settings, const std::vector<Command*>& commands, core::GeminiProvider* gemini,
                               core::LmStudioProvider* lmStudio, bool interactive, QWidget* parent)
    : QDialog(parent), m_settings(settings), m_commands(commands), m_gemini(gemini), m_lm(lmStudio), m_interactive(interactive)
{
    setWindowTitle(tr("Settings"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 12, 16, 14);
    lay->setSpacing(8);
    m_tabs = new QTabWidget;
    m_tabs->setProperty("testId", "settingsTabs");
    m_tabs->addTab(buildTranslationTab(), tr("Translation"));
    m_tabs->addTab(buildShortcutsTab(), tr("Keyboard Shortcuts"));
    m_tabs->addTab(buildGeneralTab(), tr("General"));
    lay->addWidget(m_tabs, 1);

    m_message = new QLabel;
    m_message->setWordWrap(true);
    m_message->setProperty("testId", "settingsMessage");
    m_message->hide();
    lay->addWidget(m_message);
    auto* buttons = new QHBoxLayout;
    auto* close = new QPushButton(tr("Close"));
    close->setDefault(true);
    buttons->addStretch(1);
    buttons->addWidget(close);
    lay->addLayout(buttons);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_tabs, &QTabWidget::currentChanged, m_message, &QWidget::hide);

    resize(640, 660);
    fill();
    refreshTranslation();
}

void SettingsDialog::showTab(Tab tab) { m_tabs->setCurrentIndex(int(tab)); }

// ---------------------------------------------------------------- Translation

QWidget* SettingsDialog::buildTranslationTab()
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 4, 12, 8);
    lay->setSpacing(6);

    lay->addWidget(sectionTitle(tr("GEMINI (GOOGLE, ONLINE)")));
    lay->addWidget(muted(tr("Answers first when the computer is online. The free tier needs no card; Google uses what it "
                            "receives to improve its products. The key is kept in %1, never in a file.").arg(storeName())));
    m_useGemini = new QCheckBox(tr("Use Gemini"));
    m_useGemini->setProperty("testId", "useGemini");
    lay->addWidget(m_useGemini);
    m_keyStatus = new QLabel;
    m_keyStatus->setWordWrap(true);
    m_keyStatus->setProperty("testId", "keyStatus");
    lay->addWidget(m_keyStatus);
    auto* keyRow = new QHBoxLayout;
    m_keyEdit = new QLineEdit;
    m_keyEdit->setEchoMode(QLineEdit::Password);
    m_keyEdit->setPlaceholderText(tr("Paste a Gemini API key"));
    m_keyEdit->setProperty("testId", "keyEdit");
    auto* save = new QPushButton(tr("Save Key"));
    save->setProperty("testId", "saveKey");
    m_removeKey = new QPushButton(tr("Remove Key"));
    m_removeKey->setProperty("testId", "removeKey");
    keyRow->addWidget(m_keyEdit, 1);
    keyRow->addWidget(save);
    keyRow->addWidget(m_removeKey);
    lay->addLayout(keyRow);
    auto* getKey = new QPushButton(tr("Get a Free Key…"));
    getKey->setToolTip(QStringLiteral("https://aistudio.google.com/apikey"));
    getKey->setProperty("testId", "getKey");
    getKey->setObjectName(QStringLiteral("Link"));
    getKey->setCursor(Qt::PointingHandCursor);
    lay->addWidget(getKey, 0, Qt::AlignLeft);
    auto* modelRow = new QHBoxLayout;
    modelRow->addWidget(new QLabel(tr("Model")));
    m_model = new QComboBox;
    m_model->setEditable(true);
    m_model->setProperty("testId", "geminiModel");
    m_model->addItems({QString::fromLatin1(core::GeminiProvider::kDefaultModel), QStringLiteral("gemini-3.5-flash-lite"),
                       QStringLiteral("gemini-3.1-flash-lite"), QStringLiteral("gemini-flash-latest")});
    m_model->setToolTip(tr("gemini-flash-lite-latest always uses Google's newest Flash-Lite model, which has the most "
                           "free lookups a day. Flash models are better but have far fewer free lookups."));
    modelRow->addWidget(m_model, 1);
    auto* test = new QPushButton(tr("Test"));
    test->setToolTip(tr("Looks up one word (uses one of today's free lookups)."));
    test->setProperty("testId", "testGemini");
    modelRow->addWidget(test);
    lay->addLayout(modelRow);
    m_geminiResult = muted(QString());
    m_geminiResult->setProperty("testId", "geminiResult");
    lay->addWidget(m_geminiResult);

    lay->addWidget(sectionTitle(tr("LM STUDIO (ON THIS COMPUTER)")));
    lay->addWidget(muted(tr("Asked when Gemini can't answer (offline, or today's free lookups are used up). FreeReader uses "
                            "the model you have loaded in LM Studio and never loads one itself.")));
    m_useLm = new QCheckBox(tr("Use LM Studio"));
    m_useLm->setProperty("testId", "useLmStudio");
    lay->addWidget(m_useLm);
    auto* lmRow = new QHBoxLayout;
    lmRow->addWidget(new QLabel(tr("Address")));
    m_lmAddress = new QLineEdit;
    m_lmAddress->setProperty("testId", "lmAddress");
    m_lmAddress->setPlaceholderText(QStringLiteral("http://localhost:1234"));
    lmRow->addWidget(m_lmAddress, 1);
    auto* check = new QPushButton(tr("Check"));
    check->setProperty("testId", "checkLmStudio");
    lmRow->addWidget(check);
    lay->addLayout(lmRow);
    m_lmResult = muted(QString());
    m_lmResult->setProperty("testId", "lmResult");
    lay->addWidget(m_lmResult);

    lay->addWidget(sectionTitle(tr("WHILE READING")));
    m_auto = new QCheckBox(tr("Translate Selected Text Right Away"));
    m_auto->setToolTip(tr("Off: right-click the selection and choose Translate."));
    m_auto->setProperty("testId", "autoTranslate");
    lay->addWidget(m_auto);
    m_offline = new QCheckBox(tr("Offline Only (Never Send Text Over the Internet)"));
    m_offline->setToolTip(tr("Gemini is not asked. Single books can be kept offline from the View menu."));
    m_offline->setProperty("testId", "offlineOnly");
    lay->addWidget(m_offline);
    lay->addStretch(1);

    connect(save, &QPushButton::clicked, this, [this] {
        const QString err = saveGeminiKey(m_keyEdit->text());
        m_message->setText(err.isEmpty() ? tr("The key is saved in %1.").arg(storeName()) : err);
        m_message->show();
    });
    connect(m_keyEdit, &QLineEdit::returnPressed, save, &QPushButton::click);
    connect(m_removeKey, &QPushButton::clicked, this, &SettingsDialog::removeGeminiKey);
    connect(getKey, &QPushButton::clicked, this, [this] {
        if (m_interactive) QDesktopServices::openUrl(QUrl(QStringLiteral("https://aistudio.google.com/apikey")));
    });
    connect(test, &QPushButton::clicked, this, &SettingsDialog::testGemini);
    connect(check, &QPushButton::clicked, this, &SettingsDialog::checkLmStudio);
    connect(m_useGemini, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.useGemini = on;
        emit translationChanged();
    });
    connect(m_useLm, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.useLmStudio = on;
        emit translationChanged();
    });
    connect(m_auto, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.autoTranslate = on;
        emit translationChanged();
    });
    connect(m_offline, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.offlineOnly = on;
        emit translationChanged();
    });
    connect(m_model, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        const QString v = t.trimmed() == QLatin1String(core::GeminiProvider::kDefaultModel) ? QString() : t.trimmed();
        if (v == m_settings.geminiModel) return;
        m_settings.geminiModel = v;
        emit translationChanged();
    });
    connect(m_lmAddress, &QLineEdit::editingFinished, this, [this] {
        const QString v = m_lmAddress->text().trimmed();
        if (v == m_settings.lmStudioAddress) return;
        m_settings.lmStudioAddress = v.isEmpty() ? QStringLiteral("http://localhost:1234") : v;
        emit translationChanged();
    });
    return page;
}

void SettingsDialog::refreshTranslation()
{
    const QSignalBlocker b1(m_useGemini), b2(m_useLm), b3(m_auto), b4(m_offline), b5(m_model);
    m_useGemini->setChecked(m_settings.useGemini);
    m_useLm->setChecked(m_settings.useLmStudio);
    m_auto->setChecked(m_settings.autoTranslate);
    m_offline->setChecked(m_settings.offlineOnly);
    m_model->setCurrentText(m_settings.geminiModel.isEmpty() ? QString::fromLatin1(core::GeminiProvider::kDefaultModel) : m_settings.geminiModel);
    m_lmAddress->setText(m_settings.lmStudioAddress);
    const bool has = m_gemini && m_gemini->hasKey();
    m_keyStatus->setText(has ? tr("A key is saved in %1.").arg(storeName()) : tr("No key yet."));
    m_removeKey->setEnabled(has);
}

QString SettingsDialog::saveGeminiKey(const QString& key)
{
    const QString k = key.trimmed();
    if (k.isEmpty()) return tr("Paste the key first.");
    if (k.size() < 20 || k.contains(QLatin1Char(' '))) return tr("That doesn't look like a Gemini API key.");
    QString err;
    const bool ok = core::secrets::write(kGeminiKey, k, &err);
    if (m_gemini) m_gemini->setKey(k);   // in use now even when the store refused it (then for this session)
    m_keyEdit->clear();
    refreshTranslation();
    emit translationChanged();
    return ok ? QString() : err;
}

void SettingsDialog::removeGeminiKey()
{
    core::secrets::remove(kGeminiKey);
    if (m_gemini) m_gemini->setKey(QString());
    refreshTranslation();
    m_geminiResult->clear();
    emit translationChanged();
}

void SettingsDialog::testGemini()
{
    if (!m_gemini || !m_gemini->hasKey()) {
        m_geminiResult->setText(tr("Save a key first."));
        return;
    }
    m_geminiResult->setText(tr("Asking Gemini…"));
    core::TranslationRequest r;
    r.text = QStringLiteral("biblioteca");
    r.sentence = QStringLiteral("Pasó la tarde en la biblioteca.");
    r.from = QStringLiteral("es");
    const bool wasEnabled = m_settings.useGemini;
    m_gemini->setEnabled(true);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    QPointer<SettingsDialog> self(this);
    m_gemini->translate(r, [self, timer, wasEnabled](const core::TranslationResult& res, core::TranslationFailure f, const QString& detail) {
        if (!self) return;
        self->m_gemini->setEnabled(wasEnabled);
        const QString secs = QLocale().toString(double(timer->elapsed()) / 1000.0, 'f', 1);
        if (f == core::TranslationFailure::None)
            self->m_geminiResult->setText(tr("Working: “biblioteca” is “%1” (%2 s).").arg(res.translation, secs));
        else
            self->m_geminiResult->setText(tr("Not working: %1.").arg(core::failureText(f)) + (detail.isEmpty() ? QString() : QStringLiteral(" ") + detail));
    });
}

void SettingsDialog::checkLmStudio()
{
    if (!m_lm) return;
    m_lm->setAddress(m_lmAddress->text());
    m_lmResult->setText(tr("Checking…"));
    QPointer<SettingsDialog> self(this);
    m_lm->checkModel([self](const QString& model, core::TranslationFailure f) {
        if (!self) return;
        if (f == core::TranslationFailure::None)
            self->m_lmResult->setText(tr("Running, with %1 loaded.").arg(model));
        else if (f == core::TranslationFailure::NoModel)
            self->m_lmResult->setText(tr("Running, but no model is loaded. Load one in LM Studio to use it."));
        else
            self->m_lmResult->setText(tr("Not running at %1. Start LM Studio's server (Developer tab) to use it.").arg(self->m_lm->address()));
    });
}

// ---------------------------------------------------------------- Shortcuts

QWidget* SettingsDialog::buildShortcutsTab()
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 10, 12, 8);
    lay->setSpacing(8);
    lay->addWidget(muted(tr("Click a shortcut and press the new keys. Backspace clears it.")));
    m_list = new QTreeWidget;
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({tr("Command"), tr("Shortcut")});
    m_list->setRootIsDecorated(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_list->header()->resizeSection(1, 200);
    m_list->header()->setStretchLastSection(false);
    m_list->setMinimumHeight(300);
    m_list->setProperty("testId", "shortcuts");
    lay->addWidget(m_list, 1);
    auto* resetKeys = new QPushButton(tr("Reset Shortcuts"));
    resetKeys->setToolTip(tr("Every command gets its original keys back."));
    resetKeys->setProperty("testId", "resetShortcuts");
    lay->addWidget(resetKeys, 0, Qt::AlignLeft);
    connect(resetKeys, &QPushButton::clicked, this, &SettingsDialog::resetShortcuts);
    return page;
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
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [this, id = c->id](const QKeySequence& k) {
            if (k.isEmpty()) setShortcut(id, k);   // the clear button
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

// ---------------------------------------------------------------- General

QWidget* SettingsDialog::buildGeneralTab()
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 4, 12, 8);
    lay->setSpacing(8);
    lay->addWidget(sectionTitle(tr("APP DATA FOLDER")));
    auto* folderRow = new QHBoxLayout;
    m_folder = new QLabel;
    m_folder->setWordWrap(true);
    m_folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_folder->setProperty("testId", "dataFolder");
    folderRow->addWidget(m_folder, 1);
    auto* change = new QPushButton(tr("Change…"));
    change->setToolTip(tr("Where the library database (reading places, collections, translations looked up) and the "
                          "settings are kept. Books stay where they are."));
    folderRow->addWidget(change);
    lay->addLayout(folderRow);
    connect(change, &QPushButton::clicked, this, &SettingsDialog::changeDataFolderRequested);

    lay->addWidget(sectionTitle(tr("RESET")));
    auto* resetLook = new QPushButton(tr("Reset Text and Layout"));
    resetLook->setToolTip(tr("Font, size, spacing, margins, page width and columns for all books go back to the defaults. "
                             "Settings made for a language or a single book stay."));
    resetLook->setProperty("testId", "resetLook");
    lay->addWidget(resetLook, 0, Qt::AlignLeft);
    lay->addStretch(1);
    connect(resetLook, &QPushButton::clicked, this, [this] {
        emit resetTextAndLayoutRequested();
        m_message->setText(tr("Text and layout for all books are back to the defaults."));
        m_message->show();
    });
    return page;
}

void SettingsDialog::setDataFolder(const QString& folder, bool temporary)
{
    m_folder->setText(temporary ? tr("Not kept: a temporary folder that is deleted when the app closes.") : QDir::toNativeSeparators(folder));
}

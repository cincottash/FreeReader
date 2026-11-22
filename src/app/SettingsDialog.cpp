#include "SettingsDialog.h"

#include "Settings.h"

#include "core/services/Secrets.h"
#include "core/translate/AiProviders.h"
#include "core/translate/OfflineDictionary.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>

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
#include <QScrollArea>
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
    // The Translation tab scrolls when the window is short.
    auto* scroll = new QScrollArea;
    scroll->setWidget(buildTranslationTab());
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_tabs->addTab(scroll, tr("Translation"));
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
    lay->addWidget(muted(tr("Asked when Gemini can't answer (offline, or today's free lookups are used up). LM Studio's server "
                            "must be running. With a model chosen, LM Studio loads it the first time it's needed (that "
                            "takes a moment, and uses the graphics card's memory until LM Studio unloads it).")));
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
    auto* lmModelRow = new QHBoxLayout;
    lmModelRow->addWidget(new QLabel(tr("Model")));
    m_lmModel = new QComboBox;
    m_lmModel->setProperty("testId", "lmModel");
    m_lmModel->setToolTip(tr("Check fills this list with the models LM Studio has."));
    lmModelRow->addWidget(m_lmModel, 1);
    auto* lmTest = new QPushButton(tr("Test"));
    lmTest->setToolTip(tr("Looks up one word with LM Studio."));
    lmTest->setProperty("testId", "testLmStudio");
    lmModelRow->addWidget(lmTest);
    lay->addLayout(lmModelRow);
    connect(lmTest, &QPushButton::clicked, this, &SettingsDialog::testLmStudio);
    connect(m_lmModel, &QComboBox::currentIndexChanged, this, [this] {
        const QString v = m_lmModel->currentData().toString();
        if (v == m_settings.lmStudioModel) return;
        m_settings.lmStudioModel = v;
        emit translationChanged();
    });
    m_lmResult = muted(QString());
    m_lmResult->setProperty("testId", "lmResult");
    lay->addWidget(m_lmResult);

    lay->addWidget(sectionTitle(tr("OFFLINE DICTIONARY")));
    lay->addWidget(muted(tr("Answers single words at once, before Gemini's answer arrives, and on its own when nothing else can. "
                            "It finds verb forms and plurals (“tenía” → “tener”).")));
    m_useDict = new QCheckBox(tr("Use the Offline Dictionary"));
    m_useDict->setProperty("testId", "useDictionary");
    lay->addWidget(m_useDict);
    auto* dictRow = new QHBoxLayout;
    m_dictStatus = new QLabel;
    m_dictStatus->setWordWrap(true);
    m_dictStatus->setProperty("testId", "dictionaryStatus");
    dictRow->addWidget(m_dictStatus, 1);
    auto* addDict = new QPushButton(tr("Add Dictionary File…"));
    addDict->setProperty("testId", "addDictionary");
    m_removeDict = new QPushButton(tr("Remove"));
    m_removeDict->setProperty("testId", "removeDictionary");
    dictRow->addWidget(addDict);
    dictRow->addWidget(m_removeDict);
    lay->addLayout(dictRow);
    connect(m_useDict, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.useDictionary = on;
        emit translationChanged();
    });
    connect(addDict, &QPushButton::clicked, this, [this] {
        if (!m_interactive) return;
        const QString f = QFileDialog::getOpenFileName(this, tr("Add Dictionary File"), QString(), tr("FreeReader dictionaries (*.sqlite)"));
        if (f.isEmpty()) return;
        const QString err = installDictionary(f);
        m_message->setText(err.isEmpty() ? tr("The dictionary is installed.") : err);
        m_message->show();
    });
    connect(m_removeDict, &QPushButton::clicked, this, &SettingsDialog::removeDictionary);

    lay->addWidget(sectionTitle(tr("WHILE READING")));
    m_auto = new QCheckBox(tr("Translate When Text Is Selected"));
    m_auto->setToolTip(tr("Off: selecting only selects. Right-click the selection and choose Translate, or press Ctrl+E."));
    m_auto->setProperty("testId", "translateOnSelect");
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
        m_settings.translateOnSelect = on;
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
    m_auto->setChecked(m_settings.translateOnSelect);
    m_offline->setChecked(m_settings.offlineOnly);
    m_model->setCurrentText(m_settings.geminiModel.isEmpty() ? QString::fromLatin1(core::GeminiProvider::kDefaultModel) : m_settings.geminiModel);
    m_lmAddress->setText(m_settings.lmStudioAddress);
    fillLmModels({});
    {
        const QSignalBlocker b6(m_useDict);
        m_useDict->setChecked(m_settings.useDictionary);
    }
    if (m_dict && m_dict->info().ok) {
        const core::DictionaryInfo& i = m_dict->info();
        m_dictStatus->setText(tr("%1 → English: %2 words. %3 (%4).")
                                  .arg(QLocale::languageToString(QLocale(i.language).language()), QLocale().toString(i.entries), i.source, i.license));
    } else {
        m_dictStatus->setText(tr("Not installed."));
    }
    m_removeDict->setEnabled(m_dict && m_dict->info().ok);
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

void SettingsDialog::setDictionary(core::OfflineDictionary* dictionary, const QString& installPath)
{
    m_dict = dictionary;
    m_dictPath = installPath;
    refreshTranslation();
}

QString SettingsDialog::installDictionary(const QString& file)
{
    if (!m_dict) return tr("No dictionary support.");
    const core::DictionaryInfo info = core::OfflineDictionary::inspect(file);
    if (!info.ok) return tr("That file can't be used: %1").arg(info.error);
    if (info.language != QLatin1String("es")) return tr("That dictionary is for another language (%1).").arg(info.language);
    if (QFileInfo(file).absoluteFilePath() == QFileInfo(m_dictPath).absoluteFilePath()) return {};
    QDir().mkpath(QFileInfo(m_dictPath).absolutePath());
    // The app's own copy is replaced; the chosen file is never touched.
    m_dict->close();
    const QString temp = m_dictPath + QStringLiteral(".new");
    QFile::remove(temp);
    if (!QFile::copy(file, temp)) {
        m_dict->open(m_dictPath);
        return tr("The file couldn't be copied into the app data folder.");
    }
    QFile::remove(m_dictPath);
    QFile::rename(temp, m_dictPath);
    m_dict->open(m_dictPath);
    refreshTranslation();
    emit translationChanged();
    return m_dict->info().ok ? QString() : tr("The copy can't be opened: %1").arg(m_dict->info().error);
}

void SettingsDialog::removeDictionary()
{
    if (!m_dict) return;
    m_dict->close();
    QFile::remove(m_dictPath);   // the app's own copy in its data folder
    refreshTranslation();
    emit translationChanged();
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
    m_lm->listModels([self](const QList<core::LmStudioProvider::Model>& models, core::TranslationFailure f) {
        if (!self) return;
        if (f != core::TranslationFailure::None) {
            self->m_lmResult->setText(tr("Not running at %1. Start LM Studio's server (Developer tab, or “lms server start”) to use it.")
                                          .arg(self->m_lm->address()));
            return;
        }
        self->fillLmModels(models);
        QStringList loaded;
        for (const auto& m : models)
            if (m.loaded) loaded << m.id;
        QString text = loaded.isEmpty() ? tr("Running, %1 available, none loaded.").arg(models.size())
                                        : tr("Running, %1 available; loaded: %2.").arg(models.size()).arg(loaded.join(QStringLiteral(", ")));
        if (models.isEmpty()) text = tr("Running, but LM Studio has no language models. Download one in LM Studio.");
        else if (self->m_settings.lmStudioModel.isEmpty() && loaded.isEmpty())
            text += QLatin1Char(' ') + tr("Choose a model above, or load one in LM Studio.");
        self->m_lmResult->setText(text);
    });
}

void SettingsDialog::fillLmModels(const QList<core::LmStudioProvider::Model>& models)
{
    const QSignalBlocker b(m_lmModel);
    m_lmModel->clear();
    m_lmModel->addItem(tr("The Model Loaded in LM Studio"), QString());
    bool found = false;
    for (const auto& m : models) {
        m_lmModel->addItem(m.loaded ? tr("%1 (loaded)").arg(m.id) : m.id, m.id);
        if (m.id == m_settings.lmStudioModel) found = true;
    }
    // A chosen model stays in the list before Check runs (or when LM Studio is off).
    if (!m_settings.lmStudioModel.isEmpty() && !found) m_lmModel->addItem(m_settings.lmStudioModel, m_settings.lmStudioModel);
    m_lmModel->setCurrentIndex(qMax(0, m_lmModel->findData(m_settings.lmStudioModel)));
}

void SettingsDialog::testLmStudio()
{
    if (!m_lm) return;
    m_lm->setAddress(m_lmAddress->text());
    m_lm->setModel(m_settings.lmStudioModel);
    m_lmResult->setText(tr("Asking LM Studio… (the first answer can take a while if the model has to be loaded)"));
    core::TranslationRequest r;
    r.text = QStringLiteral("biblioteca");
    r.sentence = QStringLiteral("Pasó la tarde en la biblioteca.");
    r.from = QStringLiteral("es");
    const bool wasEnabled = m_settings.useLmStudio;
    m_lm->setEnabled(true);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    QPointer<SettingsDialog> self(this);
    m_lm->translate(r, [self, timer, wasEnabled](const core::TranslationResult& res, core::TranslationFailure f, const QString& detail) {
        if (!self) return;
        self->m_lm->setEnabled(wasEnabled);
        const QString secs = QLocale().toString(double(timer->elapsed()) / 1000.0, 'f', 1);
        if (f == core::TranslationFailure::None)
            self->m_lmResult->setText(tr("Working: “biblioteca” is “%1” (%2 s, %3).")
                                          .arg(res.translation, secs,
                                               self->m_settings.lmStudioModel.isEmpty() ? tr("the loaded model") : self->m_settings.lmStudioModel));
        else
            self->m_lmResult->setText(tr("Not working: %1.").arg(core::failureText(f)) + (detail.isEmpty() ? QString() : QStringLiteral(" ") + detail));
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

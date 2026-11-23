#include "SettingsDialog.h"

#include "Settings.h"

#include "core/services/Secrets.h"
#include "core/translate/AiProviders.h"
#include "core/translate/OfflineDictionary.h"
#include "core/translate/OfflineTranslator.h"

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
#include <QListWidget>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPointer>
#include <QProgressBar>
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
    // OK keeps the changes (they apply as they are made, so they can be seen); Cancel puts back the settings as
    // they were when the window opened (the owner's request, 2026-11-23). A saved key, a download and a moved data
    // folder are done at once by their own buttons and stay.
    auto* buttons = new QHBoxLayout;
    auto* ok = new QPushButton(tr("OK"));
    ok->setDefault(true);
    ok->setProperty("testId", "settingsOk");
    auto* cancel = new QPushButton(tr("Cancel"));
    cancel->setProperty("testId", "settingsCancel");
    cancel->setToolTip(tr("Puts back the settings as they were when this window opened. A saved key, a download and "
                          "a moved data folder stay."));
    buttons->addStretch(1);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    lay->addLayout(buttons);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_tabs, &QTabWidget::currentChanged, m_message, &QWidget::hide);

    resize(640, 660);
    fill();
    refreshTranslation();
}

void SettingsDialog::showTab(Tab tab) { m_tabs->setCurrentIndex(int(tab)); }

void SettingsDialog::begin()
{
    m_before = std::make_shared<Settings>(m_settings);
    m_message->hide();
}

void SettingsDialog::reject()
{
    // Only what this window changes goes back; reading options changed meanwhile elsewhere stay.
    if (m_before) {
        const Settings& b = *m_before;
        Settings& s = m_settings;
        s.translateOnSelect = b.translateOnSelect;
        s.offlineOnly = b.offlineOnly;
        s.useGemini = b.useGemini;
        s.geminiModel = b.geminiModel;
        s.useLmStudio = b.useLmStudio;
        s.lmStudioAddress = b.lmStudioAddress;
        s.lmStudioModel = b.lmStudioModel;
        s.useDictionary = b.useDictionary;
        s.useOfflineTranslator = b.useOfflineTranslator;
        s.shortcuts = b.shortcuts;
        s.mouseShortcuts = b.mouseShortcuts;
        s.libraryFolders = b.libraryFolders;
        if (m_lookReset) s.look = b.look;
        m_before.reset();
        m_lookReset = false;
        fill();
        fillLibraryFolders();
        refreshTranslation();
        emit cancelled();
    }
    QDialog::reject();
}

void SettingsDialog::accept()
{
    m_before.reset();
    m_lookReset = false;
    QDialog::accept();
}

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

    lay->addWidget(sectionTitle(tr("OFFLINE DICTIONARIES")));
    lay->addWidget(muted(tr("Answers single words with no internet, when Gemini and LM Studio can't (or are off), "
                            "with no internet. Spanish → English finds verb forms and plurals (“tenía” → “tener”); English "
                            "definitions are for English books. Spanish definitions fill the Dictionary card (right-click a word > "
                            "Dictionary) for Spanish books. Each is an optional download.")));
    m_useDict = new QCheckBox(tr("Use the Offline Dictionaries"));
    m_useDict->setProperty("testId", "useDictionary");
    lay->addWidget(m_useDict);
    connect(m_useDict, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.useDictionary = on;
        emit translationChanged();
    });
    m_rows[0].language = QStringLiteral("es");
    m_rows[0].source = core::DictionaryDownload::spanishEnglish();
    m_rows[1].language = QStringLiteral("en");
    m_rows[1].source = core::DictionaryDownload::englishDefinitions();
    m_rows[2].language = QStringLiteral("es-es");
    m_rows[2].source = core::DictionaryDownload::spanishDefinitions();
    for (DictRow& d : m_rows) {
        const QByteArray suffix = d.language == QLatin1String("es") ? QByteArray() : QByteArray("-") + d.language.toLatin1();
        auto* rowLay = new QHBoxLayout;
        d.status = new QLabel;
        d.status->setWordWrap(true);
        d.status->setProperty("testId", ("dictionaryStatus" + suffix).constData());
        rowLay->addWidget(d.status, 1);
        d.download = new QPushButton;
        d.download->setProperty("testId", ("downloadDictionary" + suffix).constData());
        d.download->setObjectName(QStringLiteral("Suggested"));
        d.download->setToolTip(d.language == QLatin1String("es")
                                   ? tr("Downloads the Spanish → English dictionary made from Wiktionary (CC BY-SA 4.0) from "
                                        "FreeReader's page on GitHub, once. Nothing else is sent.")
                               : d.language == QLatin1String("en")
                                   ? tr("Downloads the English dictionary made from Open English WordNet (CC BY 4.0) from "
                                        "FreeReader's page on GitHub, once. Nothing else is sent.")
                                   : tr("Downloads the Spanish dictionary of definitions made from the Spanish Wiktionary "
                                        "(CC BY-SA 4.0) from FreeReader's page on GitHub, once. Nothing else is sent."));
        d.add = new QPushButton(tr("Add File…"));
        d.add->setToolTip(tr("Install a FreeReader dictionary file (.sqlite) from this computer."));
        d.add->setProperty("testId", ("addDictionary" + suffix).constData());
        d.remove = new QPushButton(tr("Remove"));
        d.remove->setProperty("testId", ("removeDictionary" + suffix).constData());
        rowLay->addWidget(d.download);
        rowLay->addWidget(d.add);
        rowLay->addWidget(d.remove);
        lay->addLayout(rowLay);
        d.progress = new QProgressBar;
        d.progress->setProperty("testId", ("dictionaryProgress" + suffix).constData());
        d.progress->setTextVisible(true);
        d.progress->hide();
        lay->addWidget(d.progress);
        const QString language = d.language;
        connect(d.download, &QPushButton::clicked, this, [this, language] {
            if (isDownloadingDictionary(language)) cancelDictionaryDownload(language);
            else downloadDictionary(language);
        });
        connect(d.add, &QPushButton::clicked, this, [this] {
            if (!m_interactive) return;
            const QString f = QFileDialog::getOpenFileName(this, tr("Add Dictionary File"), QString(), tr("FreeReader dictionaries (*.sqlite)"));
            if (f.isEmpty()) return;
            const QString err = installDictionary(f);
            m_message->setText(err.isEmpty() ? tr("The dictionary is installed.") : err);
            m_message->show();
        });
        connect(d.remove, &QPushButton::clicked, this, [this, language] { removeDictionary(language); });
    }

    lay->addWidget(sectionTitle(tr("OFFLINE TRANSLATOR (SENTENCES)")));
    lay->addWidget(muted(tr("Translates whole sentences and pages from Spanish with no internet, when Gemini and LM Studio "
                            "can't answer. It's Bergamot, the engine Firefox translates with, and Mozilla's Spanish → English "
                            "model: plainer English than Gemini's, and weaker on old Spanish. An optional download.")));
    m_useTranslator = new QCheckBox(tr("Use the Offline Translator"));
    m_useTranslator->setProperty("testId", "useOfflineTranslator");
    lay->addWidget(m_useTranslator);
    connect(m_useTranslator, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.useOfflineTranslator = on;
        emit translationChanged();
    });
    m_translatorSource = core::DictionaryDownload::offlineTranslator();
    {
        auto* rowLay = new QHBoxLayout;
        m_translatorStatus = new QLabel;
        m_translatorStatus->setWordWrap(true);
        m_translatorStatus->setProperty("testId", "translatorStatus");
        rowLay->addWidget(m_translatorStatus, 1);
        m_translatorGet = new QPushButton;
        m_translatorGet->setProperty("testId", "downloadTranslator");
        m_translatorGet->setObjectName(QStringLiteral("Suggested"));
        m_translatorGet->setToolTip(tr("Downloads the translator (Bergamot, MPL 2.0) and Mozilla's Spanish → English model "
                                       "(MPL 2.0) from FreeReader's page on GitHub, once. Nothing else is sent."));
        m_translatorRemove = new QPushButton(tr("Remove"));
        m_translatorRemove->setProperty("testId", "removeTranslator");
        rowLay->addWidget(m_translatorGet);
        rowLay->addWidget(m_translatorRemove);
        lay->addLayout(rowLay);
        m_translatorProgress = new QProgressBar;
        m_translatorProgress->setProperty("testId", "translatorProgress");
        m_translatorProgress->setTextVisible(true);
        m_translatorProgress->hide();
        lay->addWidget(m_translatorProgress);
        connect(m_translatorGet, &QPushButton::clicked, this, [this] {
            if (isDownloadingTranslator()) cancelTranslatorDownload();
            else downloadTranslator();
        });
        connect(m_translatorRemove, &QPushButton::clicked, this, &SettingsDialog::removeTranslator);
    }

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
    for (DictRow& d : m_rows) {
        const QString title = d.language == QLatin1String("es") ? tr("Spanish → English")
                            : d.language == QLatin1String("en") ? tr("English definitions")
                                                                : tr("Spanish definitions");
        const bool installed = d.dict && d.dict->info().ok;
        if (installed) {
            const core::DictionaryInfo& i = d.dict->info();
            d.status->setText(tr("%1: %2 words. %3 (%4).").arg(title, QLocale().toString(i.entries), i.source, i.license));
        } else {
            d.status->setText(tr("%1: not installed.").arg(title));
        }
        const bool downloading = isDownloadingDictionary(d.language);
        d.remove->setEnabled(installed && !downloading);
        // Download is offered while it isn't installed (and becomes Cancel while it runs).
        d.download->setVisible(d.dict && (downloading || !installed));
        d.download->setText(downloading ? tr("Cancel Download") : tr("Download (%1 MB)").arg((d.source.size + 500000) / 1000000));
        d.add->setEnabled(d.dict && !downloading);
        d.progress->setVisible(downloading);
    }
    {
        const QSignalBlocker b7(m_useTranslator);
        m_useTranslator->setChecked(m_settings.useOfflineTranslator);
        const bool installed = m_translatorEngine && m_translatorEngine->isInstalled();
        const bool downloading = isDownloadingTranslator();
        const bool offered = m_translatorSource.size > 0;   // a pack is built for this system
        m_translatorStatus->setText(installed  ? tr("Spanish → English: installed.")
                                    : offered ? tr("Spanish → English: not installed.")
                                              : tr("Not available for this system yet."));
        m_translatorGet->setVisible(m_translatorEngine && offered && (downloading || !installed));
        m_translatorGet->setText(downloading ? tr("Cancel Download")
                                             : tr("Download (%1 MB)").arg((m_translatorSource.size + 500000) / 1000000));
        m_translatorRemove->setEnabled(installed && !downloading);
        m_translatorProgress->setVisible(downloading);
    }
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

SettingsDialog::DictRow* SettingsDialog::row(const QString& language)
{
    for (DictRow& d : m_rows)
        if (d.language == language) return &d;
    return nullptr;
}

const SettingsDialog::DictRow* SettingsDialog::row(const QString& language) const
{
    for (const DictRow& d : m_rows)
        if (d.language == language) return &d;
    return nullptr;
}

void SettingsDialog::setDictionary(core::OfflineDictionary* dictionary, const QString& installPath)
{
    m_rows[0].dict = dictionary;
    m_rows[0].path = installPath;
    refreshTranslation();
}

void SettingsDialog::setEnglishDictionary(core::OfflineDictionary* dictionary, const QString& installPath)
{
    m_rows[1].dict = dictionary;
    m_rows[1].path = installPath;
    refreshTranslation();
}

void SettingsDialog::setDictionaryFor(const QString& key, core::OfflineDictionary* dictionary, const QString& installPath)
{
    if (DictRow* d = row(key)) {
        d->dict = dictionary;
        d->path = installPath;
    }
    refreshTranslation();
}

QString SettingsDialog::dictionaryKey(const core::DictionaryInfo& info)
{
    // Spanish -> English "es", English definitions "en", Spanish definitions "es-es".
    if (info.language == QLatin1String("es") && info.target == QLatin1String("en")) return QStringLiteral("es");
    if (info.language == QLatin1String("en") && info.target == QLatin1String("en")) return QStringLiteral("en");
    if (info.language == QLatin1String("es") && info.target == QLatin1String("es")) return QStringLiteral("es-es");
    return {};
}

void SettingsDialog::setDictionarySource(const core::DictionaryDownload::Source& s, const QString& language)
{
    if (DictRow* d = row(language)) d->source = s;
    refreshTranslation();
}

QString SettingsDialog::installDictionary(const QString& file)
{
    const core::DictionaryInfo info = core::OfflineDictionary::inspect(file);
    if (!info.ok) return tr("That file can't be used: %1").arg(info.error);
    // The file says which dictionary it is: Spanish -> English, English definitions, or Spanish definitions.
    DictRow* d = row(dictionaryKey(info));
    if (!d)
        return tr("That dictionary is for another language (%1).").arg(info.language + (info.target.isEmpty() ? QString() : QStringLiteral(" → ") + info.target));
    if (!d->dict) return tr("No dictionary support.");
    if (QFileInfo(file).absoluteFilePath() == QFileInfo(d->path).absoluteFilePath()) return {};
    QDir().mkpath(QFileInfo(d->path).absolutePath());
    // The app's own copy is replaced; the chosen file is never touched.
    d->dict->close();
    const QString temp = d->path + QStringLiteral(".new");
    QFile::remove(temp);
    if (!QFile::copy(file, temp)) {
        d->dict->open(d->path);
        return tr("The file couldn't be copied into the app data folder.");
    }
    QFile::remove(d->path);
    QFile::rename(temp, d->path);
    d->dict->open(d->path);
    refreshTranslation();
    emit translationChanged();
    return d->dict->info().ok ? QString() : tr("The copy can't be opened: %1").arg(d->dict->info().error);
}

bool SettingsDialog::isDownloadingDictionary(const QString& language) const
{
    for (const DictRow& d : m_rows)
        if ((language.isEmpty() || d.language == language) && d.downloader && d.downloader->isRunning()) return true;
    return false;
}

QString SettingsDialog::lastMessage() const { return m_message->isVisible() || !m_message->text().isEmpty() ? m_message->text() : QString(); }

void SettingsDialog::downloadDictionary(const QString& language)
{
    DictRow* d = row(language);
    if (!d || !d->dict || isDownloadingDictionary(language)) return;
    if (!d->downloader) {
        d->downloader = new core::DictionaryDownload(this);
        QProgressBar* bar = d->progress;
        connect(d->downloader, &core::DictionaryDownload::progress, this, [bar](qint64 got, qint64 total) {
            bar->setRange(0, 1000);
            bar->setValue(total > 0 ? int(qMin<qint64>(1000, got * 1000 / total)) : 0);
            bar->setFormat(tr("%1 of %2 MB").arg(QLocale().toString(double(got) / 1e6, 'f', 1), QLocale().toString(double(total) / 1e6, 'f', 1)));
        });
        connect(d->downloader, &core::DictionaryDownload::finished, this,
                [this, language](const QString& file, const QString& error) { installDownloaded(language, file, error); });
    }
    d->progress->setRange(0, 0);   // busy until the first bytes arrive
    d->progress->setFormat(tr("Connecting…"));
    m_message->setText(tr("Downloading the dictionary…"));
    m_message->show();
    d->downloader->start(d->source, d->path + QStringLiteral(".download"));
    refreshTranslation();
}

void SettingsDialog::cancelDictionaryDownload(const QString& language)
{
    if (DictRow* d = row(language); d && d->downloader) d->downloader->cancel();
}

void SettingsDialog::installDownloaded(const QString& language, const QString& file, const QString& error)
{
    DictRow* d = row(language);
    if (!d) return;
    if (!error.isEmpty()) {
        QFile::remove(file);
        m_message->setText(error);
        m_message->show();
        refreshTranslation();
        return;
    }
    const core::DictionaryInfo info = core::OfflineDictionary::inspect(file);
    if (!info.ok || dictionaryKey(info) != language) {
        QFile::remove(file);
        m_message->setText(tr("The downloaded dictionary can't be used: %1").arg(info.ok ? info.language : info.error));
        m_message->show();
        refreshTranslation();
        return;
    }
    // Into place (a dictionary added meanwhile is replaced).
    d->dict->close();
    QFile::remove(d->path);
    if (!QFile::rename(file, d->path)) {
        QFile::remove(file);
        m_message->setText(tr("The dictionary couldn't be put in the app data folder."));
    } else {
        d->dict->open(d->path);
        m_message->setText(d->dict->info().ok ? tr("The dictionary is installed: %1 words.").arg(QLocale().toString(d->dict->info().entries))
                                              : tr("The dictionary can't be opened: %1").arg(d->dict->info().error));
    }
    m_message->show();
    refreshTranslation();
    emit translationChanged();
}

void SettingsDialog::removeDictionary(const QString& language)
{
    DictRow* d = row(language);
    if (!d || !d->dict) return;
    d->dict->close();
    QFile::remove(d->path);   // the app's own copy in its data folder
    refreshTranslation();
    emit translationChanged();
}

// ---------------------------------------------------------------- Offline translator

void SettingsDialog::setOfflineTranslator(core::OfflineTranslator* translator)
{
    m_translatorEngine = translator;
    refreshTranslation();
}

void SettingsDialog::setTranslatorSource(const core::DictionaryDownload::Source& s)
{
    m_translatorSource = s;
    refreshTranslation();
}

bool SettingsDialog::isDownloadingTranslator() const { return m_translatorDownload && m_translatorDownload->isRunning(); }

namespace {
// The translator's own folder in the app data folder, and nothing else: the only folder the app ever replaces or
// removes as a whole. Empty when the path isn't one (then nothing is removed).
QString ownTranslatorFolder(const core::OfflineTranslator* t)
{
    if (!t) return {};
    const QString f = QDir::cleanPath(t->folder());
    return QFileInfo(f).fileName() == QLatin1String("translator") && QFileInfo(f).isAbsolute() ? f : QString();
}
}  // namespace

void SettingsDialog::downloadTranslator()
{
    const QString folder = ownTranslatorFolder(m_translatorEngine);
    if (folder.isEmpty() || isDownloadingTranslator() || m_translatorSource.size <= 0) return;
    if (!m_translatorDownload) {
        m_translatorDownload = new core::DictionaryDownload(this);
        QProgressBar* bar = m_translatorProgress;
        connect(m_translatorDownload, &core::DictionaryDownload::progress, this, [bar](qint64 got, qint64 total) {
            bar->setRange(0, 1000);
            bar->setValue(total > 0 ? int(qMin<qint64>(1000, got * 1000 / total)) : 0);
            bar->setFormat(tr("%1 of %2 MB").arg(QLocale().toString(double(got) / 1e6, 'f', 1), QLocale().toString(double(total) / 1e6, 'f', 1)));
        });
        connect(m_translatorDownload, &core::DictionaryDownload::finished, this, &SettingsDialog::installTranslator);
    }
    // Unpacked next to the folder in use, then swapped in, so a failed download leaves the installed one working.
    QDir(folder + QStringLiteral(".download")).removeRecursively();
    m_translatorProgress->setRange(0, 0);
    m_translatorProgress->setFormat(tr("Connecting…"));
    m_message->setText(tr("Downloading the offline translator…"));
    m_message->show();
    m_translatorDownload->start(m_translatorSource, folder + QStringLiteral(".download"));
    refreshTranslation();
}

void SettingsDialog::cancelTranslatorDownload()
{
    if (m_translatorDownload) m_translatorDownload->cancel();
}

void SettingsDialog::installTranslator(const QString& unpacked, const QString& error)
{
    const QString folder = ownTranslatorFolder(m_translatorEngine);
    auto done = [this](const QString& message) {
        m_message->setText(message);
        m_message->show();
        refreshTranslation();
        emit translationChanged();
    };
    if (folder.isEmpty() || unpacked != folder + QStringLiteral(".download")) return done(tr("The offline translator wasn't installed."));
    if (!error.isEmpty()) {
        QDir(unpacked).removeRecursively();
        return done(error);
    }
    core::OfflineTranslator check;
    check.setFolder(unpacked);
#ifndef Q_OS_WIN
    // Zip files don't keep the "can be run" mark.
    QFile::setPermissions(check.program(), QFile::permissions(check.program()) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
#endif
    if (!check.isInstalled()) {
        QDir(unpacked).removeRecursively();
        return done(tr("The download doesn't contain the translator and its model, so it wasn't installed."));
    }
    QDir(folder).removeRecursively();
    if (!QDir().rename(unpacked, folder)) {
        QDir(unpacked).removeRecursively();
        return done(tr("The translator couldn't be put in the app data folder."));
    }
    m_translatorEngine->setFolder(folder);
    done(tr("The offline translator is installed."));
}

void SettingsDialog::removeTranslator()
{
    const QString folder = ownTranslatorFolder(m_translatorEngine);
    if (folder.isEmpty() || isDownloadingTranslator()) return;
    QDir(folder).removeRecursively();   // the app's own copy in its data folder
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
    lay->addWidget(muted(tr("Click a shortcut and press the new keys. Backspace clears it. Under Mouse, click and then "
                            "press the middle, back or forward button (with Ctrl, Shift or Alt if you like); Backspace "
                            "clears it.")));
    m_list = new QTreeWidget;
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({tr("Command"), tr("Shortcut"), tr("Mouse")});
    m_list->setRootIsDecorated(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_list->header()->resizeSection(1, 200);
    m_list->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_list->header()->resizeSection(2, 170);
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
    fillLibraryFolders();
    m_list->clear();
    for (Command* c : m_commands) {
        auto* it = new QTreeWidgetItem(m_list, {c->label});
        it->setToolTip(0, c->hint);
        it->setData(0, Qt::UserRole, c->id);
        auto* edit = new QKeySequenceEdit(c->action->shortcut());
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        edit->setProperty("commandId", c->id);
        edit->setMinimumHeight(edit->sizeHint().height());
        // The row as tall as the editor (the theme pads text fields), so its text isn't cut off.
        it->setSizeHint(1, QSize(200, edit->sizeHint().height() + 6));
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
        // The mouse button: click, then press it.
        auto* mouse = new QPushButton(c->mouse.isEmpty() ? QStringLiteral("—") : mouse_shortcut::display(c->mouse));
        mouse->setProperty("testId", (QStringLiteral("mouse-") + c->id).toLatin1().constData());
        mouse->setProperty("commandId", c->id);
        mouse->setCheckable(true);
        mouse->setFocusPolicy(Qt::StrongFocus);
        mouse->setToolTip(tr("Click, then press the middle, back or forward mouse button. Backspace clears it."));
        mouse->installEventFilter(this);
        m_list->setItemWidget(it, 2, mouse);
        connect(mouse, &QPushButton::toggled, this, [this, mouse, id = c->id](bool on) {
            QString current;
            for (Command* c2 : m_commands)
                if (c2->id == id) current = c2->mouse;
            mouse->setText(on ? tr("Press a Button…") : current.isEmpty() ? QStringLiteral("—") : mouse_shortcut::display(current));
        });
    }
}

bool SettingsDialog::eventFilter(QObject* o, QEvent* e)
{
    // A Mouse cell waiting for its button: the middle, back or forward button sets it, Backspace clears it.
    auto* b = qobject_cast<QPushButton*>(o);
    if (!b || !b->isChecked() || !b->property("commandId").isValid()) return QDialog::eventFilter(o, e);
    const QString id = b->property("commandId").toString();
    if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick) {
        auto* me = static_cast<QMouseEvent*>(e);
        const QString binding = mouse_shortcut::fromEvent(me->button(), me->modifiers());
        if (binding.isEmpty()) return QDialog::eventFilter(o, e);   // a left click: cancels (unchecks)
        setMouseShortcut(id, binding);
        b->setChecked(false);
        return true;
    }
    if (e->type() == QEvent::KeyPress) {
        const int k = static_cast<QKeyEvent*>(e)->key();
        if (k == Qt::Key_Backspace || k == Qt::Key_Delete) {
            setMouseShortcut(id, QString());
            b->setChecked(false);
            return true;
        }
        if (k == Qt::Key_Escape) {
            b->setChecked(false);
            return true;
        }
    }
    return QDialog::eventFilter(o, e);
}

QString SettingsDialog::setMouseShortcut(const QString& id, const QString& binding)
{
    Command* target = nullptr;
    for (Command* c : m_commands)
        if (c->id == id) target = c;
    if (!target) return {};
    if (!binding.isEmpty())
        for (Command* c : m_commands)
            if (c != target && c->mouse == binding) {
                const QString msg = tr("%1 is already used by %2. Change that one first.").arg(mouse_shortcut::display(binding), c->label);
                m_message->setText(msg);
                m_message->show();
                return msg;
            }
    QStringList kept;
    for (const QString& o : m_settings.mouseShortcuts)
        if (o.section(QLatin1Char('='), 0, 0) != id) kept << o;
    if (!binding.isEmpty()) kept << id + QLatin1Char('=') + binding;
    m_settings.mouseShortcuts = kept;
    target->mouse = binding;
    m_message->hide();
    emit shortcutsChanged();
    return {};
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
    m_settings.mouseShortcuts.clear();
    for (Command* c : m_commands) c->mouse.clear();
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
    lay->addWidget(muted(tr("Where the library, reading places, notes, words looked up and settings are kept. Books stay "
                            "where they are.")));
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

    lay->addWidget(sectionTitle(tr("LIBRARY FOLDERS")));
    lay->addWidget(muted(tr("Each time FreeReader starts, the books put in these folders (or their subfolders) since are "
                            "added to the library. A book already in the library isn't added twice.")));
    // The list in a frame, its buttons beside it (not floating below).
    auto* foldersRow = new QHBoxLayout;
    foldersRow->setSpacing(10);
    m_libraryFolders = new QListWidget;
    m_libraryFolders->setProperty("testId", "libraryFolders");
    m_libraryFolders->setFrameShape(QFrame::StyledPanel);
    m_libraryFolders->setObjectName(QStringLiteral("FramedList"));   // a border in every theme (Theme.cpp)
    m_libraryFolders->setFixedHeight(104);
    foldersRow->addWidget(m_libraryFolders, 1);
    auto* folderButtons = new QVBoxLayout;
    folderButtons->setSpacing(6);
    auto* addFolder = new QPushButton(tr("Add Folder…"));
    addFolder->setToolTip(tr("A folder of books: its books are added now and its new books at every start."));
    addFolder->setProperty("testId", "addLibraryFolder");
    m_removeFolder = new QPushButton(tr("Remove Folder"));
    m_removeFolder->setToolTip(tr("FreeReader stops checking this folder. Its books stay in the library."));
    m_removeFolder->setProperty("testId", "removeLibraryFolder");
    m_removeFolder->setEnabled(false);
    folderButtons->addWidget(addFolder);
    folderButtons->addWidget(m_removeFolder);
    folderButtons->addStretch(1);
    foldersRow->addLayout(folderButtons);
    lay->addLayout(foldersRow);
    connect(addFolder, &QPushButton::clicked, this, &SettingsDialog::addLibraryFolderRequested);
    connect(m_removeFolder, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* it = m_libraryFolders->currentItem()) removeLibraryFolder(it->data(Qt::UserRole).toString());
    });
    connect(m_libraryFolders, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it) {
        m_removeFolder->setEnabled(it && !it->data(Qt::UserRole).toString().isEmpty());
    });

    // Text and layout: what the button resets, said beside it.
    lay->addWidget(sectionTitle(tr("TEXT AND LAYOUT")));
    auto* lookRow = new QHBoxLayout;
    lookRow->setSpacing(10);
    lookRow->addWidget(muted(tr("Font, size, spacing, margins, page width and columns for all books. Settings made for one "
                                "language or one book stay as they are.")), 1);
    auto* resetLook = new QPushButton(tr("Reset to Defaults"));
    resetLook->setToolTip(tr("Font, size, spacing, margins, page width and columns for all books go back to the defaults."));
    resetLook->setProperty("testId", "resetLook");
    lookRow->addWidget(resetLook, 0, Qt::AlignTop);
    lay->addLayout(lookRow);
    lay->addStretch(1);
    connect(resetLook, &QPushButton::clicked, this, [this] {
        m_lookReset = true;
        emit resetTextAndLayoutRequested();
        m_message->setText(tr("Text and layout for all books are back to the defaults (Cancel puts them back)."));
        m_message->show();
    });
    return page;
}

void SettingsDialog::fillLibraryFolders()
{
    m_libraryFolders->clear();
    for (const QString& f : m_settings.libraryFolders) {
        auto* it = new QListWidgetItem(QDir::toNativeSeparators(f), m_libraryFolders);
        it->setData(Qt::UserRole, f);
        if (!QFileInfo(f).isDir()) it->setText(it->text() + tr("  (not found now)"));
    }
    if (m_settings.libraryFolders.isEmpty()) {
        auto* it = new QListWidgetItem(tr("No folders yet: Add Folder… picks one."), m_libraryFolders);
        it->setFlags(Qt::NoItemFlags);
    }
    if (m_removeFolder) m_removeFolder->setEnabled(false);
}

void SettingsDialog::removeLibraryFolder(const QString& folder)
{
    m_settings.libraryFolders.removeAll(folder);
    fillLibraryFolders();
    emit libraryFoldersChanged();
}

void SettingsDialog::setDataFolder(const QString& folder, bool temporary)
{
    m_folder->setText(temporary ? tr("Not kept: a temporary folder that is deleted when the app closes.") : QDir::toNativeSeparators(folder));
}

#include "SettingsDialog.h"

#include "Settings.h"

#include "core/services/Secrets.h"
#include "core/translate/AiProviders.h"
#include "core/translate/LanguagePacks.h"
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
#include <QGridLayout>
#include <QMap>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabWidget>

#include <algorithm>
#include <cmath>
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
    // General first, then Translation, Audio and Keyboard Shortcuts (the owner's order, 2026-11-23).
    // General scrolls too: it holds the languages and their packs.
    auto* generalScroll = new QScrollArea;
    generalScroll->setWidget(buildGeneralTab());
    generalScroll->setWidgetResizable(true);
    generalScroll->setFrameShape(QFrame::NoFrame);
    m_tabs->addTab(generalScroll, tr("General"));
    m_tabs->addTab(scroll, tr("Translation"));
    m_tabs->addTab(buildAudioTab(), tr("Audio"));
    m_tabs->addTab(buildShortcutsTab(), tr("Keyboard Shortcuts"));
    lay->addWidget(m_tabs, 1);
    // The voices are listed when the Audio tab is shown: the system's speech engine starts only then.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        if (i == int(AudioTab)) fillVoices();
    });

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

void SettingsDialog::showTab(Tab tab)
{
    m_tabs->setCurrentIndex(int(tab));
    if (tab == AudioTab) fillVoices();
}

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
        s.offlineBooks = b.offlineBooks;
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
        const bool audio = s.speechRatePct != b.speechRatePct || s.speechVolumePct != b.speechVolumePct || s.speechVoices != b.speechVoices;
        s.speechRatePct = b.speechRatePct;
        s.speechVolumePct = b.speechVolumePct;
        s.speechVoices = b.speechVoices;
        const bool language = s.readerLanguage != b.readerLanguage;
        s.readerLanguage = b.readerLanguage;
        core::languages::setReaderLanguage(s.readerLanguage);
        if (language) buildPackRows();
        if (m_lookReset) s.look = b.look;
        m_before.reset();
        m_lookReset = false;
        fill();
        if (audio) {
            fillVoices();
            emit audioChanged();
        }
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
    // The key, model and address boxes: one moderate width (a key or a model's name fits), their buttons right
    // beside them, and the labels of both sections in one column (the owner's request, 2026-11-25: boxes as wide
    // as the window looked odd).
    const int fieldWidth = page->fontMetrics().averageCharWidth() * 46;
    QList<QLabel*> fieldLabels;
    auto fieldGrid = [&] {
        auto* g = new QGridLayout;
        g->setHorizontalSpacing(8);
        g->setVerticalSpacing(6);
        g->setColumnStretch(3, 1);
        return g;
    };
    auto fieldLabel = [&](const QString& text) {
        auto* l = new QLabel(text);
        fieldLabels << l;
        return l;
    };
    auto buttons = [](std::initializer_list<QPushButton*> list) {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        for (QPushButton* b : list) row->addWidget(b);
        row->addStretch(1);   // each button its own size, not the width of the key's two
        return row;
    };
    auto* gemini = fieldGrid();
    m_keyEdit = new QLineEdit;
    m_keyEdit->setEchoMode(QLineEdit::Password);
    m_keyEdit->setPlaceholderText(tr("Paste a Gemini API key"));
    m_keyEdit->setProperty("testId", "keyEdit");
    m_keyEdit->setFixedWidth(fieldWidth);
    auto* save = new QPushButton(tr("Save Key"));
    save->setProperty("testId", "saveKey");
    m_removeKey = new QPushButton(tr("Remove Key"));
    m_removeKey->setProperty("testId", "removeKey");
    gemini->addWidget(fieldLabel(tr("Key")), 0, 0);
    gemini->addWidget(m_keyEdit, 0, 1);
    gemini->addLayout(buttons({save, m_removeKey}), 0, 2);
    auto* getKey = new QPushButton(tr("Get a Free Key…"));
    getKey->setToolTip(QStringLiteral("https://aistudio.google.com/apikey"));
    getKey->setProperty("testId", "getKey");
    getKey->setObjectName(QStringLiteral("Link"));
    getKey->setCursor(Qt::PointingHandCursor);
    gemini->addWidget(getKey, 1, 1, Qt::AlignLeft);
    m_model = new QComboBox;
    m_model->setEditable(true);
    m_model->setProperty("testId", "geminiModel");
    m_model->addItems({QString::fromLatin1(core::GeminiProvider::kDefaultModel), QStringLiteral("gemini-3.5-flash-lite"),
                       QStringLiteral("gemini-3.1-flash-lite"), QStringLiteral("gemini-flash-latest")});
    m_model->setToolTip(tr("gemini-flash-lite-latest always uses Google's newest Flash-Lite model, which has the most "
                           "free lookups a day. Flash models are better but have far fewer free lookups."));
    m_model->setFixedWidth(fieldWidth);
    auto* test = new QPushButton(tr("Test"));
    test->setToolTip(tr("Looks up one word (uses one of today's free lookups)."));
    test->setProperty("testId", "testGemini");
    gemini->addWidget(fieldLabel(tr("Model")), 2, 0);
    gemini->addWidget(m_model, 2, 1);
    gemini->addLayout(buttons({test}), 2, 2);
    lay->addLayout(gemini);
    m_geminiResult = muted(QString());
    m_geminiResult->setProperty("testId", "geminiResult");
    lay->addWidget(m_geminiResult);

    lay->addWidget(sectionTitle(tr("LM STUDIO (ON THIS COMPUTER)")));
    lay->addWidget(muted(tr("Asked when Gemini can't answer (offline, today's free lookups are used up, or no answer to a word or "
                            "passage within 2.5 seconds). LM Studio's server "
                            "must be running. With a model chosen, LM Studio loads it the first time it's needed (that "
                            "takes a moment, and uses the graphics card's memory until LM Studio unloads it).")));
    m_useLm = new QCheckBox(tr("Use LM Studio"));
    m_useLm->setProperty("testId", "useLmStudio");
    lay->addWidget(m_useLm);
    auto* lm = fieldGrid();
    m_lmAddress = new QLineEdit;
    m_lmAddress->setProperty("testId", "lmAddress");
    m_lmAddress->setPlaceholderText(QStringLiteral("http://localhost:1234"));
    m_lmAddress->setFixedWidth(fieldWidth);
    // Both buttons say Test (the owner's request, 2026-11-25): this one tests the address, the one below the model.
    auto* check = new QPushButton(tr("Test"));
    check->setProperty("testId", "checkLmStudio");
    check->setToolTip(tr("Connects to LM Studio at this address and lists its models."));
    lm->addWidget(fieldLabel(tr("Address")), 0, 0);
    lm->addWidget(m_lmAddress, 0, 1);
    lm->addLayout(buttons({check}), 0, 2);
    m_lmModel = new QComboBox;
    m_lmModel->setProperty("testId", "lmModel");
    m_lmModel->setToolTip(tr("Test beside the address fills this list with the models LM Studio has."));
    m_lmModel->setFixedWidth(fieldWidth);
    auto* lmTest = new QPushButton(tr("Test"));
    lmTest->setToolTip(tr("Looks up one word with LM Studio."));
    lmTest->setProperty("testId", "testLmStudio");
    lm->addWidget(fieldLabel(tr("Model")), 1, 0);
    lm->addWidget(m_lmModel, 1, 1);
    lm->addLayout(buttons({lmTest}), 1, 2);
    lay->addLayout(lm);
    // One column for the labels of both sections, so the four boxes line up.
    int labelWidth = 0;
    for (QLabel* l : fieldLabels) labelWidth = qMax(labelWidth, l->sizeHint().width());
    gemini->setColumnMinimumWidth(0, labelWidth);
    lm->setColumnMinimumWidth(0, labelWidth);
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
    lay->addWidget(muted(tr("Answers single words with no internet, when Gemini and LM Studio can't (or are off). Spanish → "
                            "English finds verb forms and plurals (“tenía” → “tener”); English → Spanish is for readers whose "
                            "language is Spanish. English and Spanish definitions are for books in your own language, and fill "
                            "the Dictionary card (right-click a word > Dictionary). Each is an optional download.")));
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
    m_rows[3].language = QStringLiteral("en-es");
    m_rows[3].source = core::DictionaryDownload::englishSpanish();
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
                               : d.language == QLatin1String("en-es")
                                   ? tr("Downloads the English → Spanish dictionary made from Wiktionary (CC BY-SA 4.0) from "
                                        "FreeReader's page on GitHub, once. Nothing else is sent.")
                                   : tr("Downloads the Spanish dictionary of definitions made from the Spanish Wiktionary "
                                        "(CC BY-SA 4.0) from FreeReader's page on GitHub, once. Nothing else is sent."));
        d.remove = new QPushButton(tr("Uninstall"));
        d.remove->setProperty("testId", ("removeDictionary" + suffix).constData());
        rowLay->addWidget(d.download);
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
        connect(d.remove, &QPushButton::clicked, this, [this, language] { removeDictionary(language); });
    }

    lay->addWidget(sectionTitle(tr("OFFLINE TRANSLATOR (SENTENCES)")));
    lay->addWidget(muted(tr("Translates whole sentences and pages with no internet, when Gemini and LM Studio can't answer. "
                            "It's Bergamot, the engine Firefox translates with, and Mozilla's models (Spanish → English comes "
                            "with it; English → Spanish comes with the English pack for Spanish readers): plainer than "
                            "Gemini's, and weaker on old Spanish. An optional download.")));
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
        m_translatorRemove = new QPushButton(tr("Uninstall"));
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

    // At the bottom, after every engine is described (the owner's request, 2026-11-25: not in the middle of Gemini's
    // section). A book is let back online by opening it and clearing the box (no button empties the whole list).
    lay->addWidget(sectionTitle(tr("PRIVACY")));
    lay->addWidget(muted(tr("Gemini is the only engine that sends text over the internet: with Use Gemini off, nothing "
                            "leaves this computer. A single book can be kept offline instead; the engines on this "
                            "computer still answer for it.")));
    m_bookOffline = new QCheckBox(tr("Keep This Book Offline"));
    m_bookOffline->setProperty("testId", "bookOffline");
    m_bookOffline->setToolTip(tr("Text from the open book is never sent to Gemini. Also in Settings > Translation in the menu bar."));
    lay->addWidget(m_bookOffline);
    m_offlineCount = muted(QString());
    m_offlineCount->setProperty("testId", "offlineCount");
    lay->addWidget(m_offlineCount);
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
        refreshOfflineBooks();
        emit translationChanged();
    });
    connect(m_bookOffline, &QCheckBox::toggled, this, &SettingsDialog::setOpenBookOffline);
    connect(m_useLm, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.useLmStudio = on;
        emit translationChanged();
    });
    connect(m_auto, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.translateOnSelect = on;
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
    const QSignalBlocker b1(m_useGemini), b2(m_useLm), b3(m_auto), b5(m_model);
    m_useGemini->setChecked(m_settings.useGemini);
    m_useLm->setChecked(m_settings.useLmStudio);
    m_auto->setChecked(m_settings.translateOnSelect);
    refreshOfflineBooks();
    m_model->setCurrentText(m_settings.geminiModel.isEmpty() ? QString::fromLatin1(core::GeminiProvider::kDefaultModel) : m_settings.geminiModel);
    m_lmAddress->setText(m_settings.lmStudioAddress);
    fillLmModels({});
    {
        const QSignalBlocker b6(m_useDict);
        m_useDict->setChecked(m_settings.useDictionary);
    }
    for (DictRow& d : m_rows) {
        const QString title = d.language == QLatin1String("es")      ? tr("Spanish → English")
                              : d.language == QLatin1String("en")    ? tr("English definitions")
                              : d.language == QLatin1String("en-es") ? tr("English → Spanish")
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
        d.progress->setVisible(downloading);
    }
    {
        const QSignalBlocker b7(m_useTranslator);
        m_useTranslator->setChecked(m_settings.useOfflineTranslator);
        const bool installed = m_translatorEngine && m_translatorEngine->isInstalled();
        const bool downloading = isDownloadingTranslator();
        const bool offered = m_translatorSource.size > 0;   // a pack is built for this system
        QStringList pairs;
        if (installed)
            for (const QString& p : m_translatorEngine->pairs())
                pairs << QStringLiteral("%1 → %2").arg(core::languages::displayName(p.section(QLatin1Char('-'), 0, 0)),
                                                        core::languages::displayName(p.section(QLatin1Char('-'), 1, 1)));
        m_translatorStatus->setText(installed  ? tr("Installed: %1.").arg(pairs.join(QStringLiteral(", ")))
                                    : offered ? tr("Not installed.")
                                              : tr("Not available for this system yet."));
        m_translatorGet->setVisible(m_translatorEngine && offered && (downloading || !installed));
        m_translatorGet->setText(downloading ? tr("Cancel Download")
                                             : tr("Download (%1 MB)").arg((m_translatorSource.size + 500000) / 1000000));
        m_translatorRemove->setEnabled(installed && !downloading);
        m_translatorProgress->setVisible(downloading);
    }
    {
        // Every Download button one width, the widest any of them needs (its size, or Cancel Download while it runs),
        // so they line up whatever the sizes (the owner's request, 2026-11-25).
        QList<QPushButton*> gets;
        for (DictRow& d : m_rows) gets << d.download;
        gets << m_translatorGet;
        int width = 0;
        for (QPushButton* b : gets) {
            const QString shown = b->text();
            for (const QString& t : {shown, tr("Cancel Download")}) {
                b->setText(t);
                width = qMax(width, b->sizeHint().width());
            }
            b->setText(shown);
        }
        for (QPushButton* b : gets) b->setFixedWidth(width);
    }
    const bool has = m_gemini && m_gemini->hasKey();
    m_keyStatus->setText(has ? tr("A key is saved in %1.").arg(storeName()) : tr("No key yet."));
    m_removeKey->setEnabled(has);
    refreshPacks();   // the language packs are made of these same dictionaries and the translator
}

void SettingsDialog::refreshOfflineBooks()
{
    const std::pair<int, QString> open = m_openBook ? m_openBook() : std::pair<int, QString>{0, QString()};
    const QSignalBlocker b(m_bookOffline);
    m_bookOffline->setChecked(open.first && m_settings.offlineBooks.contains(QString::number(open.first)));
    // With Gemini off every book is offline already.
    m_bookOffline->setEnabled(open.first && m_settings.useGemini);
    // The book's own title goes in the line beside it, not in the switch (labels are title case, titles aren't).
    const QString book = open.first ? tr("Open now: %1.").arg(open.second.isEmpty() ? tr("Untitled") : open.second)
                                    : tr("No book is open.");
    const int n = int(m_settings.offlineBooks.size());
    const QString count = n == 0   ? tr("No book is kept offline.")
                          : n == 1 ? tr("1 book is kept offline.")
                                   : tr("%1 books are kept offline.").arg(QLocale().toString(n));
    m_offlineCount->setText(book + QLatin1Char(' ') + count);
}

void SettingsDialog::setOpenBookOffline(bool on)
{
    const int id = m_openBook ? m_openBook().first : 0;
    if (id) {
        m_settings.offlineBooks.removeAll(QString::number(id));
        if (on) m_settings.offlineBooks << QString::number(id);
        emit translationChanged();
    }
    refreshOfflineBooks();
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
    if (info.language == QLatin1String("en") && info.target == QLatin1String("es")) return QStringLiteral("en-es");
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
        connect(d->downloader, &core::DictionaryDownload::progress, this, [this, bar, language](qint64 got, qint64 total) {
            bar->setRange(0, 1000);
            bar->setValue(total > 0 ? int(qMin<qint64>(1000, got * 1000 / total)) : 0);
            bar->setFormat(tr("%1 of %2 MB").arg(QLocale().toString(double(got) / 1e6, 'f', 1), QLocale().toString(double(total) / 1e6, 'f', 1)));
            if (m_packPart == language) packProgress(got);
        });
        connect(d->downloader, &core::DictionaryDownload::finished, this, [this, language](const QString& file, const QString& error) {
            installDownloaded(language, file, error);
            packPartDone(language);
        });
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
        connect(m_translatorDownload, &core::DictionaryDownload::progress, this, [this, bar](qint64 got, qint64 total) {
            bar->setRange(0, 1000);
            bar->setValue(total > 0 ? int(qMin<qint64>(1000, got * 1000 / total)) : 0);
            bar->setFormat(tr("%1 of %2 MB").arg(QLocale().toString(double(got) / 1e6, 'f', 1), QLocale().toString(double(total) / 1e6, 'f', 1)));
            if (m_packPart == QLatin1String("translator")) packProgress(got);
        });
        connect(m_translatorDownload, &core::DictionaryDownload::finished, this, [this](const QString& unpacked, const QString& error) {
            installTranslator(unpacked, error);
            packPartDone(QStringLiteral("translator"));
        });
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
    // Models downloaded on their own ("en-es") stay: moved into the new folder before the old one goes.
    if (m_translatorEngine)
        for (const QString& pair : m_translatorEngine->pairs())
            if (!check.hasPair(pair.section(QLatin1Char('-'), 0, 0), pair.section(QLatin1Char('-'), 1, 1)))
                QDir().rename(QDir(folder).filePath(pair), QDir(unpacked).filePath(pair));
    QDir(folder).removeRecursively();
    if (!QDir().rename(unpacked, folder)) {
        QDir(unpacked).removeRecursively();
        return done(tr("The translator couldn't be put in the app data folder."));
    }
    m_translatorEngine->setFolder(folder);
    done(tr("The offline translator is installed."));
}

bool SettingsDialog::isDownloadingTranslatorModel() const { return m_modelDownload && m_modelDownload->isRunning(); }

void SettingsDialog::setTranslatorModelSource(const QString& pair, const core::DictionaryDownload::Source& s)
{
    m_modelSources.insert(pair, s);
}

void SettingsDialog::downloadTranslatorModel(const QString& pair)
{
    const QString folder = ownTranslatorFolder(m_translatorEngine);
    const core::DictionaryDownload::Source source = m_modelSources.value(pair, core::DictionaryDownload::translatorModel(pair));
    if (folder.isEmpty() || isDownloadingTranslatorModel() || source.size <= 0) return;
    if (!m_modelDownload) {
        m_modelDownload = new core::DictionaryDownload(this);
        connect(m_modelDownload, &core::DictionaryDownload::progress, this, [this](qint64 got, qint64) {
            if (m_packPart == QStringLiteral("model-") + m_modelPair) packProgress(got);
        });
        connect(m_modelDownload, &core::DictionaryDownload::finished, this, [this](const QString& unpacked, const QString& error) {
            const QString pair = std::exchange(m_modelPair, QString());
            installTranslatorModel(pair, unpacked, error);
            packPartDone(QStringLiteral("model-") + pair);
        });
    }
    m_modelPair = pair;
    // Unpacked beside the translator's folder, then its pair folder moved in.
    const QString temp = folder + QStringLiteral(".model.download");
    QDir(temp).removeRecursively();
    m_message->setText(tr("Downloading the translation model…"));
    m_message->show();
    m_modelDownload->start(source, temp);
}

void SettingsDialog::cancelTranslatorModelDownload()
{
    if (m_modelDownload) m_modelDownload->cancel();
}

void SettingsDialog::installTranslatorModel(const QString& pair, const QString& unpacked, const QString& error)
{
    const QString folder = ownTranslatorFolder(m_translatorEngine);
    auto done = [this](const QString& message) {
        m_message->setText(message);
        m_message->show();
        refreshTranslation();
        emit translationChanged();
    };
    if (folder.isEmpty() || unpacked != folder + QStringLiteral(".model.download")) return done(tr("The translation model wasn't installed."));
    if (!error.isEmpty()) {
        QDir(unpacked).removeRecursively();
        return done(error);
    }
    const QStringList two = pair.split(QLatin1Char('-'));
    core::OfflineTranslator check;
    check.setFolder(unpacked);
    if (two.size() != 2 || !check.hasPair(two[0], two[1])) {
        QDir(unpacked).removeRecursively();
        return done(tr("The download doesn't contain the translation model, so it wasn't installed."));
    }
    QDir().mkpath(folder);
    QDir(QDir(folder).filePath(pair)).removeRecursively();
    const bool moved = QDir().rename(QDir(unpacked).filePath(pair), QDir(folder).filePath(pair));
    QDir(unpacked).removeRecursively();
    if (!moved) return done(tr("The translation model couldn't be put in the app data folder."));
    m_translatorEngine->setFolder(folder);
    done(tr("The translation model is installed."));
}

void SettingsDialog::removeTranslatorModel(const QString& pair)
{
    const QString folder = ownTranslatorFolder(m_translatorEngine);
    if (folder.isEmpty() || pair.isEmpty() || pair.contains(QLatin1Char('/')) || pair.contains(QLatin1Char('\\')) || pair.contains(QLatin1String("..")))
        return;
    QDir(QDir(folder).filePath(pair)).removeRecursively();   // the app's own copy in its data folder
    refreshTranslation();
    emit translationChanged();
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
    const core::TranslationRequest r = testRequest();
    const bool wasEnabled = m_settings.useGemini;
    m_gemini->setEnabled(true);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    QPointer<SettingsDialog> self(this);
    m_gemini->translate(r, [self, timer, wasEnabled, r](const core::TranslationResult& res, core::TranslationFailure f, const QString& detail) {
        if (!self) return;
        self->m_gemini->setEnabled(wasEnabled);
        const QString secs = QLocale().toString(double(timer->elapsed()) / 1000.0, 'f', 1);
        if (f == core::TranslationFailure::None)
            self->m_geminiResult->setText(tr("Working: “%1” is “%2” (%3 s).").arg(r.text, res.translation, secs));
        else
            self->m_geminiResult->setText(tr("Not working: %1.").arg(core::failureText(f)) + (detail.isEmpty() ? QString() : QStringLiteral(" ") + detail));
    });
}

core::TranslationRequest SettingsDialog::testRequest()
{
    // A word from a book in a language the reader reads: Spanish for an English reader, English for a Spanish one.
    core::TranslationRequest r;
    if (core::languages::readerLanguage() == QLatin1String("es")) {
        r.text = QStringLiteral("library");
        r.sentence = QStringLiteral("She spent the afternoon in the library.");
        r.from = QStringLiteral("en");
    } else {
        r.text = QStringLiteral("biblioteca");
        r.sentence = QStringLiteral("Pasó la tarde en la biblioteca.");
        r.from = QStringLiteral("es");
    }
    return r;
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
    const core::TranslationRequest r = testRequest();
    const bool wasEnabled = m_settings.useLmStudio;
    m_lm->setEnabled(true);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    QPointer<SettingsDialog> self(this);
    m_lm->translate(r, [self, timer, wasEnabled, r](const core::TranslationResult& res, core::TranslationFailure f, const QString& detail) {
        if (!self) return;
        self->m_lm->setEnabled(wasEnabled);
        const QString secs = QLocale().toString(double(timer->elapsed()) / 1000.0, 'f', 1);
        if (f == core::TranslationFailure::None)
            self->m_lmResult->setText(tr("Working: “%1” is “%2” (%3 s, %4).")
                                          .arg(r.text, res.translation, secs,
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
    // Languages: the reader's own, and a pack for each language read.
    lay->addWidget(sectionTitle(tr("LANGUAGES")));
    lay->addWidget(muted(tr("FreeReader's text, translations and explanations are in this language. Each language read has a "
                            "pack: its dictionary, its definitions and the offline translator, for reading with no internet. "
                            "Gemini and LM Studio need nothing downloaded.")));
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(10);
        row->addWidget(new QLabel(tr("Language")));
        m_readerLanguage = new QComboBox;
        m_readerLanguage->setProperty("testId", "readerLanguage");
        for (const QString& code : core::languages::readerLanguages())
            m_readerLanguage->addItem(core::languages::nativeName(code), code);
        row->addWidget(m_readerLanguage);
        auto* note = muted(tr("More languages come as their packs are made complete."));
        row->addWidget(note, 1);
        lay->addLayout(row);
        connect(m_readerLanguage, &QComboBox::currentIndexChanged, this,
                [this] { setReaderLanguage(m_readerLanguage->currentData().toString()); });
        // Another language chosen: the app's text follows when the window is rebuilt.
        m_restartRow = new QWidget;
        auto* restartLay = new QHBoxLayout(m_restartRow);
        restartLay->setContentsMargins(0, 0, 0, 0);
        restartLay->setSpacing(10);
        m_restartNote = muted(QString());
        m_restartNote->setProperty("testId", "restartNote");
        restartLay->addWidget(m_restartNote, 1);
        auto* restart = new QPushButton(tr("Restart Now"));
        restart->setObjectName(QStringLiteral("Suggested"));
        restart->setProperty("testId", "restartForLanguage");
        restart->setToolTip(tr("Opens FreeReader's window again in the language chosen, with the book that is open."));
        restartLay->addWidget(restart, 0, Qt::AlignTop);
        connect(restart, &QPushButton::clicked, this, [this] {
            accept();   // the settings as they are now
            emit restartRequested();
        });
        lay->addWidget(m_restartRow);
        m_restartRow->hide();
    }
    m_packBox = new QWidget;
    m_packBox->setProperty("testId", "languagePacks");
    auto* packLay = new QVBoxLayout(m_packBox);
    packLay->setContentsMargins(0, 2, 0, 0);
    packLay->setSpacing(6);
    lay->addWidget(m_packBox);
    buildPackRows();

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

// ---------------------------------------------------------------- Languages

void SettingsDialog::buildPackRows()
{
    auto* lay = static_cast<QVBoxLayout*>(m_packBox->layout());
    while (QLayoutItem* it = lay->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        if (QLayout* l = it->layout()) {
            while (QLayoutItem* c = l->takeAt(0)) {
                if (c->widget()) c->widget()->deleteLater();
                delete c;
            }
        }
        delete it;
    }
    m_packRows.clear();
    for (const core::languages::LanguagePack& p : core::languages::packsFor(m_settings.readerLanguage)) {
        PackRow r;
        r.language = p.language;
        auto* row = new QHBoxLayout;
        row->setSpacing(10);
        auto* texts = new QVBoxLayout;
        texts->setSpacing(1);
        auto* name = new QLabel(p.language == p.reader ? tr("%1 (Definitions Only)").arg(core::languages::displayTitle(p.language))
                                                       : core::languages::displayTitle(p.language));
        name->setStyleSheet(QStringLiteral("font-weight: 600;"));
        texts->addWidget(name);
        r.status = muted(QString());
        r.status->setProperty("testId", QStringLiteral("packStatus-") + p.language);
        texts->addWidget(r.status);
        row->addLayout(texts, 1);
        r.get = new QPushButton;
        r.get->setObjectName(QStringLiteral("Suggested"));
        r.get->setProperty("testId", QStringLiteral("packDownload-") + p.language);
        r.get->setToolTip(tr("Downloads %1 from FreeReader's page on GitHub, once. Nothing else is sent.").arg(p.contents()));
        r.remove = new QPushButton(tr("Uninstall"));
        r.remove->setProperty("testId", QStringLiteral("packRemove-") + p.language);
        r.remove->setToolTip(tr("Deletes this pack's files from the app data folder. It can be downloaded again."));
        row->addWidget(r.get, 0, Qt::AlignTop);
        row->addWidget(r.remove, 0, Qt::AlignTop);
        lay->addLayout(row);
        r.progress = new QProgressBar;
        r.progress->setProperty("testId", QStringLiteral("packProgress-") + p.language);
        r.progress->setTextVisible(true);
        r.progress->hide();
        lay->addWidget(r.progress);
        const QString language = p.language;
        connect(r.get, &QPushButton::clicked, this, [this, language] {
            if (m_packLanguage == language) cancelPackDownload();
            else downloadPack(language);
        });
        connect(r.remove, &QPushButton::clicked, this, [this, language] { removePack(language); });
        m_packRows.push_back(r);
    }
    refreshPacks();
}

bool SettingsDialog::partInstalled(const QString& key) const
{
    // The program comes with its Spanish -> English model; other models come on their own ("model-en-es").
    if (key == QLatin1String("translator"))
        return m_translatorEngine && m_translatorEngine->hasProgram() && m_translatorEngine->hasPair(QStringLiteral("es"), QStringLiteral("en"));
    if (key.startsWith(QLatin1String("model-"))) {
        const QStringList two = key.mid(6).split(QLatin1Char('-'));
        return m_translatorEngine && two.size() == 2 && m_translatorEngine->hasPair(two[0], two[1]);
    }
    const DictRow* d = row(key);
    return d && d->dict && d->dict->info().ok;
}

bool SettingsDialog::isPackInstalled(const QString& language) const
{
    const std::optional<core::languages::LanguagePack> p = core::languages::packFor(m_settings.readerLanguage, language);
    if (!p) return false;
    for (const core::languages::PackPart& part : p->parts)
        if (!partInstalled(part.key)) return false;
    return true;
}

void SettingsDialog::refreshPacks()
{
    if (!m_readerLanguage) return;
    {
        const QSignalBlocker b(m_readerLanguage);
        m_readerLanguage->setCurrentIndex(qMax(0, m_readerLanguage->findData(m_settings.readerLanguage)));
    }
    refreshRestart();
    const QLocale loc;
    for (PackRow& r : m_packRows) {
        const std::optional<core::languages::LanguagePack> p = core::languages::packFor(m_settings.readerLanguage, r.language);
        if (!p) continue;
        int have = 0;
        qint64 missing = 0;
        for (const core::languages::PackPart& part : p->parts) {
            if (partInstalled(part.key)) ++have;
            else missing += part.source.size;
        }
        const bool busy = m_packLanguage == r.language;
        const QString mb = loc.toString(double(missing) / 1e6, 'f', 0);
        QString status = p->contents();
        if (busy) status += QStringLiteral(" · ") + tr("downloading…");
        else if (have == p->parts.size()) status += QStringLiteral(" · ") + tr("installed");
        else if (have > 0) status += QStringLiteral(" · ") + tr("%1 of %2 parts installed").arg(have).arg(p->parts.size());
        r.status->setText(status);
        r.get->setText(busy ? tr("Cancel Download") : have == 0 ? tr("Download (%1 MB)").arg(mb) : tr("Download the Rest (%1 MB)").arg(mb));
        r.get->setVisible(busy || have < p->parts.size());
        r.get->setEnabled(busy || m_packLanguage.isEmpty());
        r.remove->setVisible(have > 0 && !busy);
        r.progress->setVisible(busy);
    }
}

void SettingsDialog::setReaderLanguage(const QString& code)
{
    if (!core::languages::readerLanguages().contains(code) || code == m_settings.readerLanguage) return;
    m_settings.readerLanguage = code;
    core::languages::setReaderLanguage(code);
    buildPackRows();
    refreshRestart();
    emit languageChanged();
}

void SettingsDialog::setInterfaceLanguage(const QString& code)
{
    m_interfaceLanguage = code.isEmpty() ? QStringLiteral("en") : code;
    refreshRestart();
}

void SettingsDialog::refreshRestart()
{
    if (!m_restartRow) return;
    const bool other = m_settings.readerLanguage != m_interfaceLanguage;
    // Said in the language chosen, so it can be read.
    if (other)
        m_restartNote->setText(m_settings.readerLanguage == QLatin1String("es")
                                   ? QStringLiteral("El texto de FreeReader cambia a español al reiniciar la ventana.")
                                   : QStringLiteral("FreeReader's text changes to English when the window restarts."));
    m_restartRow->setVisible(other);
}

void SettingsDialog::downloadPacks(const QStringList& languages)
{
    QStringList todo;
    for (const QString& l : languages)
        if (!isPackInstalled(l)) todo << l;
    if (todo.isEmpty() || isDownloadingPack()) return;
    const QString first = todo.takeFirst();
    downloadPack(first);
    for (const QString& l : todo) m_packQueue << QStringLiteral("pack:") + l;
}

void SettingsDialog::downloadPack(const QString& language)
{
    const std::optional<core::languages::LanguagePack> p = core::languages::packFor(m_settings.readerLanguage, language);
    if (!p || isDownloadingPack() || isDownloadingDictionary() || isDownloadingTranslator() || isDownloadingTranslatorModel()) return;
    m_packLanguage = language;
    m_packDone = 0;
    QStringList parts;
    for (const core::languages::PackPart& part : p->parts)
        if (!partInstalled(part.key)) parts << part.key;
    m_packQueue = parts + m_packQueue;   // its parts first, then any packs queued after it
    for (PackRow& r : m_packRows)
        if (r.language == language) {
            r.progress->setRange(0, 0);
            r.progress->setFormat(tr("Connecting…"));
        }
    // Chosen to be read: kept with the reader's languages.
    if (!m_settings.practiceLanguages.contains(language)) {
        m_settings.practiceLanguages << language;
        emit languageChanged();
    }
    startNextPackPart();
}

void SettingsDialog::startNextPackPart()
{
    m_packPart.clear();
    while (!m_packQueue.isEmpty() && !m_packQueue.first().startsWith(QLatin1String("pack:")) && partInstalled(m_packQueue.first()))
        m_packQueue.removeFirst();
    if (m_packQueue.isEmpty() || m_packQueue.first().startsWith(QLatin1String("pack:"))) {
        // This pack is done; the next one, if any.
        const QString done = m_packLanguage;
        m_packLanguage.clear();
        refreshPacks();
        if (!done.isEmpty()) {
            m_message->setText(tr("The %1 pack is installed.").arg(core::languages::displayName(done)));
            m_message->show();
            emit packFinished(done, QString());
        }
        if (!m_packQueue.isEmpty()) {
            const QString next = m_packQueue.takeFirst().mid(5);
            downloadPack(next);
        }
        return;
    }
    m_packPart = m_packQueue.takeFirst();
    refreshPacks();
    const bool model = m_packPart.startsWith(QLatin1String("model-"));
    if (m_packPart == QLatin1String("translator")) downloadTranslator();
    else if (model) downloadTranslatorModel(m_packPart.mid(6));
    else downloadDictionary(m_packPart);
    // Nothing started (no folder for it, or no download for this computer): the pack stops with a message.
    const bool started = m_packPart == QLatin1String("translator") ? isDownloadingTranslator()
                         : model                                  ? isDownloadingTranslatorModel()
                                                                  : isDownloadingDictionary(m_packPart);
    if (!started) packPartDone(m_packPart);
}

void SettingsDialog::packProgress(qint64 got)
{
    const std::optional<core::languages::LanguagePack> p = core::languages::packFor(m_settings.readerLanguage, m_packLanguage);
    if (!p) return;
    qint64 total = 0;
    for (const core::languages::PackPart& part : p->parts) total += part.source.size;
    const qint64 now = m_packDone + got;
    for (PackRow& r : m_packRows)
        if (r.language == m_packLanguage) {
            r.progress->setRange(0, 1000);
            r.progress->setValue(total > 0 ? int(qMin<qint64>(1000, now * 1000 / total)) : 0);
            r.progress->setFormat(tr("%1 of %2 MB").arg(QLocale().toString(double(now) / 1e6, 'f', 1), QLocale().toString(double(total) / 1e6, 'f', 1)));
        }
}

void SettingsDialog::packPartDone(const QString& key)
{
    if (key != m_packPart || m_packLanguage.isEmpty()) return;
    const std::optional<core::languages::LanguagePack> p = core::languages::packFor(m_settings.readerLanguage, m_packLanguage);
    if (!partInstalled(key)) {
        // A part failed (or was cancelled): the pack stops here; the parts already installed stay.
        const QString failed = m_packLanguage;
        const QString why = m_message->text();
        m_packLanguage.clear();
        m_packPart.clear();
        m_packQueue.clear();
        refreshPacks();
        m_message->setText(tr("The %1 pack wasn't finished: %2").arg(core::languages::displayName(failed), why));
        m_message->show();
        emit packFinished(failed, why.isEmpty() ? tr("not finished") : why);
        return;
    }
    if (p)
        for (const core::languages::PackPart& part : p->parts)
            if (part.key == key) m_packDone += part.source.size;
    startNextPackPart();
}

void SettingsDialog::cancelPackDownload()
{
    if (m_packLanguage.isEmpty()) return;
    m_packQueue.clear();
    if (m_packPart == QLatin1String("translator")) cancelTranslatorDownload();
    else if (m_packPart.startsWith(QLatin1String("model-"))) cancelTranslatorModelDownload();
    else if (!m_packPart.isEmpty()) cancelDictionaryDownload(m_packPart);
}

void SettingsDialog::removePack(const QString& language)
{
    const std::optional<core::languages::LanguagePack> p = core::languages::packFor(m_settings.readerLanguage, language);
    if (!p || m_packLanguage == language) return;
    for (const core::languages::PackPart& part : p->parts) {
        if (part.key == QLatin1String("translator")) removeTranslator();   // with every model in it
        else if (part.key.startsWith(QLatin1String("model-"))) removeTranslatorModel(part.key.mid(6));
        else removeDictionary(part.key);
    }
    m_settings.practiceLanguages.removeAll(language);
    refreshPacks();
    emit languageChanged();
}

// ---------------------------------------------------------------- Audio

namespace {
QString rateText(int pct)
{
    if (pct == 0) return SettingsDialog::tr("Normal");
    return pct > 0 ? SettingsDialog::tr("%1% Faster").arg(pct) : SettingsDialog::tr("%1% Slower").arg(-pct);
}
QString languageKeyOf(const QLocale& l) { return QLocale::languageToCode(l.language(), QLocale::ISO639Part1); }
}  // namespace

QWidget* SettingsDialog::buildAudioTab()
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 4, 12, 8);
    lay->setSpacing(8);
    lay->addWidget(sectionTitle(tr("READ ALOUD AND PRONUNCIATION")));
    lay->addWidget(muted(tr("Hear Pronunciation (the speaker in the translation pop-up) and Read Aloud (the right-click menu) "
                            "use the voices installed on this computer.")));
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    auto slider = [&](int row, const QString& name, int min, int max, const char* id, QSlider*& s, QLabel*& value) {
        grid->addWidget(new QLabel(name), row, 0);
        s = new QSlider(Qt::Horizontal);
        s->setRange(min, max);
        s->setSingleStep(10);
        s->setPageStep(10);
        s->setProperty("testId", id);
        grid->addWidget(s, row, 1);
        value = new QLabel;
        value->setMinimumWidth(90);
        value->setProperty("testId", QByteArray(id) + "Label");
        grid->addWidget(value, row, 2);
    };
    slider(0, tr("Speed"), -100, 100, "speechRate", m_rate, m_rateLabel);
    slider(1, tr("Volume"), 0, 100, "speechVolume", m_volume, m_volumeLabel);
    m_rate->setToolTip(tr("How fast words and passages are read. Normal is the voice's own speed."));
    m_volume->setToolTip(tr("How loud FreeReader reads, next to the computer's own volume."));
    grid->setColumnStretch(1, 1);
    lay->addLayout(grid);
    connect(m_rate, &QSlider::valueChanged, this, [this](int v) { setSpeechRate(v); });
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) { setSpeechVolume(v); });

    lay->addWidget(sectionTitle(tr("VOICES")));
    lay->addWidget(muted(tr("The voice for each language. The system's choice is the voice set as the default for that "
                            "language on this computer.")));
    m_voiceRows = new QWidget;
    m_voiceRows->setLayout(new QGridLayout);
    m_voiceRows->layout()->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(m_voiceRows);
    m_voiceNote = muted(QString());
    m_voiceNote->setProperty("testId", "voiceNote");
    m_voiceNote->hide();
    lay->addWidget(m_voiceNote);
    m_sampleResult = muted(QString());
    m_sampleResult->setProperty("testId", "sampleResult");
    m_sampleResult->hide();
    lay->addWidget(m_sampleResult);
#ifdef Q_OS_WIN
    auto* more = new QPushButton(tr("Add Voices…"));
    more->setToolTip(tr("Opens Windows Settings > Time & Language > Speech, where more voices can be added (free)."));
    more->setProperty("testId", "addVoices");
    lay->addWidget(more, 0, Qt::AlignLeft);
    connect(more, &QPushButton::clicked, this, [this] {
        if (m_interactive) QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:speech")));
    });
#endif
    lay->addStretch(1);
    fillAudio();
    return page;
}

void SettingsDialog::fillAudio()
{
    if (!m_rate) return;
    const QSignalBlocker b1(m_rate), b2(m_volume);
    m_rate->setValue(m_settings.speechRatePct);
    m_volume->setValue(m_settings.speechVolumePct);
    m_rateLabel->setText(rateText(m_settings.speechRatePct));
    m_volumeLabel->setText(QStringLiteral("%1%").arg(m_settings.speechVolumePct));
}

void SettingsDialog::setSpeechRate(int pct)
{
    pct = qBound(-100, int(std::lround(pct / 10.0)) * 10, 100);
    if (pct == m_settings.speechRatePct && m_rate->value() == pct) return;
    m_settings.speechRatePct = pct;
    fillAudio();
    emit audioChanged();
}

void SettingsDialog::setSpeechVolume(int pct)
{
    pct = qBound(0, int(std::lround(pct / 10.0)) * 10, 100);
    if (pct == m_settings.speechVolumePct && m_volume->value() == pct) return;
    m_settings.speechVolumePct = pct;
    fillAudio();
    emit audioChanged();
}

void SettingsDialog::chooseVoice(const QString& language, const QString& voice)
{
    QStringList keep;
    for (const QString& s : m_settings.speechVoices)
        if (!s.startsWith(language + QLatin1Char('='))) keep << s;
    if (!voice.isEmpty()) keep << language + QLatin1Char('=') + voice;
    keep.sort();
    if (keep == m_settings.speechVoices) return;
    m_settings.speechVoices = keep;
    emit audioChanged();
}

QString SettingsDialog::playSample(const QString& language)
{
    const QString problem = m_playSample ? m_playSample(language) : tr("Speech isn't available.");
    m_sampleResult->setText(problem);
    m_sampleResult->setVisible(!problem.isEmpty());
    return problem;
}

void SettingsDialog::fillVoices()
{
    fillAudio();
    auto* grid = static_cast<QGridLayout*>(m_voiceRows->layout());
    while (QLayoutItem* it = grid->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    // The voices by language: each language once, its name in English.
    const QList<Voice> voices = m_voiceSource ? m_voiceSource() : QList<Voice>();
    QMap<QString, QList<Voice>> byLanguage;
    for (const Voice& v : voices) byLanguage[languageKeyOf(v.locale)] << v;
    QList<QPair<QString, QString>> languages;   // name, key
    for (auto it = byLanguage.cbegin(); it != byLanguage.cend(); ++it)
        languages << qMakePair(core::languages::displayTitle(it.key()), it.key());
    std::sort(languages.begin(), languages.end());
    int row = 0;
    for (const auto& [name, key] : languages) {
        grid->addWidget(new QLabel(name), row, 0);
        auto* box = new QComboBox;
        box->setProperty("testId", QStringLiteral("voice-") + key);
        box->addItem(tr("System's Choice"), QString());
        QString chosen;
        for (const QString& s : m_settings.speechVoices)
            if (s.startsWith(key + QLatin1Char('='))) chosen = s.mid(key.size() + 1);
        for (const Voice& v : byLanguage.value(key)) {
            const QString territory = QLocale::territoryToString(v.locale.territory());
            box->addItem(territory.isEmpty() ? v.name : QStringLiteral("%1 (%2)").arg(v.name, territory), v.name);
        }
        box->setCurrentIndex(qMax(0, box->findData(chosen)));
        grid->addWidget(box, row, 1);
        auto* play = new QPushButton(tr("Play Sample"));
        play->setProperty("testId", QStringLiteral("playSample-") + key);
        play->setToolTip(tr("A short sentence in %1, with this voice, speed and volume.").arg(name));
        grid->addWidget(play, row, 2);
        const QString k = key;
        connect(box, &QComboBox::currentIndexChanged, this, [this, box, k] { chooseVoice(k, box->currentData().toString()); });
        connect(play, &QPushButton::clicked, this, [this, k] { playSample(k); });
        ++row;
    }
    grid->setColumnStretch(1, 1);
#ifdef Q_OS_WIN
    const QString none = tr("No voices were found on this computer. Add Voices… opens the Windows settings where free voices "
                            "can be added.");
#else
    const QString none = tr("No voices were found. Install speech-dispatcher and espeak-ng (for example: sudo apt install "
                            "speech-dispatcher espeak-ng), then start FreeReader again.");
#endif
    m_voiceNote->setText(none);
    m_voiceNote->setVisible(languages.isEmpty());
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

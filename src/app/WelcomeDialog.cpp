#include "WelcomeDialog.h"

#include "InterfaceLanguage.h"
#include "Settings.h"

#include "core/translate/LanguagePacks.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QLabel* section(const QString& text)
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

}  // namespace

WelcomeDialog::WelcomeDialog(Settings& settings, std::function<bool(const QString&)> installed, const QString& dataFolder, QWidget* parent)
    : QDialog(parent), m_settings(settings), m_installed(std::move(installed)), m_dataFolder(dataFolder)
{
    setProperty("testId", "welcome");
    // The first start suggests this computer's language; later (an update) the one in use.
    m_language = settings.welcomeShown || settings.readerLanguage != QLatin1String("en") ? settings.readerLanguage
                                                                                        : core::languages::suggestedReaderLanguage();
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    if (m_language != interfaceLanguage()) installInterfaceLanguage(m_language, m_dataFolder);
    build();
    resize(580, 10);
    fitHeight();
}

void WelcomeDialog::fitHeight()
{
    // The wrapped text's height at this width (Spanish needs more lines than English).
    layout()->activate();
    const int h = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width()) : sizeHint().height();
    resize(width(), qMax(h, minimumSizeHint().height()));
}

void WelcomeDialog::build()
{
    if (m_content) {
        // Keep what was ticked; the widgets go.
        for (const Row& r : m_rows) m_ticks.insert(r.language, r.box->isChecked());
        // Out of the dialog at once (its combo box sent the signal that got here, so it's deleted later).
        m_content->hide();
        m_content->setParent(nullptr);
        m_content->deleteLater();
    }
    m_rows.clear();
    setWindowTitle(tr("Welcome to FreeReader"));
    m_content = new QWidget;
    layout()->addWidget(m_content);
    auto* lay = new QVBoxLayout(m_content);
    lay->setContentsMargins(24, 18, 24, 16);
    lay->setSpacing(8);
    auto* title = new QLabel(tr("Welcome to FreeReader"));
    title->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 600;"));
    lay->addWidget(title);
    lay->addWidget(muted(tr("Right-click a word while you read and its meaning appears beside it; every word you look up becomes a "
                            "flashcard. Choose your language and the languages you read. Each language has a pack to download, "
                            "for reading with no internet.")));

    lay->addWidget(section(tr("LANGUAGE")));
    auto* langRow = new QHBoxLayout;
    m_languageBox = new QComboBox;
    m_languageBox->setProperty("testId", "welcomeLanguage");
    for (const QString& code : core::languages::readerLanguages()) m_languageBox->addItem(core::languages::nativeName(code), code);
    m_languageBox->setCurrentIndex(qMax(0, m_languageBox->findData(m_language)));
    langRow->addWidget(m_languageBox);
    langRow->addWidget(muted(tr("FreeReader's text, translations and explanations are in it.")), 1);
    lay->addLayout(langRow);

    lay->addWidget(section(tr("LANGUAGES TO READ")));
    auto* packsBox = new QWidget;
    m_packs = new QVBoxLayout(packsBox);
    m_packs->setContentsMargins(0, 0, 0, 0);
    m_packs->setSpacing(8);
    lay->addWidget(packsBox);
    lay->addWidget(muted(tr("Nothing is downloaded until you choose Download and Start. Gemini (online, free) gives the best "
                            "answers: add a key in Settings > Translation. Packs can be downloaded or removed later in "
                            "Settings > General.")));
    lay->addStretch(1);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_later = new QPushButton(tr("Not Now"));
    m_later->setProperty("testId", "welcomeLater");
    m_start = new QPushButton;
    m_start->setObjectName(QStringLiteral("Primary"));
    m_start->setDefault(true);
    m_start->setProperty("testId", "welcomeStart");
    buttons->addWidget(m_later);
    buttons->addWidget(m_start);
    lay->addLayout(buttons);

    connect(m_languageBox, &QComboBox::currentIndexChanged, this, &WelcomeDialog::languageChosen);
    connect(m_start, &QPushButton::clicked, this, [this] { finish(true); });
    connect(m_later, &QPushButton::clicked, this, [this] { finish(false); });
    fillPacks();
}

void WelcomeDialog::languageChosen()
{
    const QString code = m_languageBox->currentData().toString();
    if (code.isEmpty() || code == m_language) return;
    m_language = code;
    // The welcome itself in the new language at once; the window behind it follows when it closes.
    installInterfaceLanguage(code, m_dataFolder);
    build();
    fitHeight();
    // Again once the new widgets are shown and styled (their sizes are final only then).
    QTimer::singleShot(0, this, &WelcomeDialog::fitHeight);
}

QString WelcomeDialog::readerLanguage() const { return m_language; }

void WelcomeDialog::setReaderLanguage(const QString& code)
{
    const int i = m_languageBox->findData(code);
    if (i >= 0) m_languageBox->setCurrentIndex(i);
}

void WelcomeDialog::fillPacks()
{
    const QLocale loc;
    // The first start ticks every pack (one click gets everything); later, the languages read before.
    const bool firstStart = m_settings.practiceLanguages.isEmpty();
    for (const core::languages::LanguagePack& p : core::languages::packsFor(m_language)) {
        Row r;
        r.language = p.language;
        r.installed = m_installed && m_installed(p.language);
        auto* holder = new QWidget;
        auto* v = new QVBoxLayout(holder);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(1);
        const QString name = core::languages::displayTitle(p.language);
        r.box = new QCheckBox(p.language == p.reader ? tr("%1 (Definitions Only)").arg(name) : name);
        r.box->setProperty("testId", QStringLiteral("welcomePack-") + p.language);
        // Installed already: ticked, nothing to download.
        const bool tick = m_ticks.value(p.language, firstStart || m_settings.practiceLanguages.contains(p.language));
        r.box->setChecked(r.installed || tick);
        r.box->setEnabled(!r.installed);
        v->addWidget(r.box);
        auto* detail = muted(p.contents() + QStringLiteral(" · ") +
                             (r.installed ? tr("installed") : tr("%1 MB").arg(loc.toString(double(p.downloadSize()) / 1e6, 'f', 0))));
        detail->setContentsMargins(26, 0, 0, 0);
        detail->setProperty("testId", QStringLiteral("welcomePackDetail-") + p.language);
        v->addWidget(detail);
        m_packs->addWidget(holder);
        connect(r.box, &QCheckBox::toggled, this, [this] { updateButtons(); });
        m_rows.push_back(r);
    }
    updateButtons();
}

QStringList WelcomeDialog::chosen() const
{
    QStringList out;
    for (const Row& r : m_rows)
        if (r.box->isChecked() && !r.installed) out << r.language;
    return out;
}

void WelcomeDialog::choose(const QString& language, bool on)
{
    for (Row& r : m_rows)
        if (r.language == language && r.box->isEnabled()) r.box->setChecked(on);
}

void WelcomeDialog::updateButtons()
{
    qint64 bytes = 0;
    for (const QString& l : chosen())
        if (const auto p = core::languages::packFor(m_language, l)) bytes += p->downloadSize();
    m_start->setText(bytes > 0 ? tr("Download and Start (%1 MB)").arg(QLocale().toString(double(bytes) / 1e6, 'f', 0)) : tr("Start Reading"));
}

void WelcomeDialog::finish(bool download)
{
    // Asked once: "Not Now" too (everything is in Settings > General).
    // The language chosen is kept either way ("Not Now" is about the downloads).
    m_settings.welcomeShown = true;
    m_settings.readerLanguage = m_language;
    core::languages::setReaderLanguage(m_settings.readerLanguage);
    if (download) {
        for (const Row& r : m_rows)
            if (r.box->isChecked() && !m_settings.practiceLanguages.contains(r.language)) m_settings.practiceLanguages << r.language;
        const QStringList get = chosen();
        accept();
        if (!get.isEmpty()) emit downloadRequested(get);
    } else {
        reject();
    }
}

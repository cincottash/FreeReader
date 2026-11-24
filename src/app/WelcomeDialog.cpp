#include "WelcomeDialog.h"

#include "Settings.h"

#include "core/translate/LanguagePacks.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
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

WelcomeDialog::WelcomeDialog(Settings& settings, std::function<bool(const QString&)> installed, QWidget* parent)
    : QDialog(parent), m_settings(settings), m_installed(std::move(installed))
{
    setWindowTitle(tr("Welcome to FreeReader"));
    setProperty("testId", "welcome");
    auto* lay = new QVBoxLayout(this);
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
    m_language = new QComboBox;
    m_language->setProperty("testId", "welcomeLanguage");
    for (const QString& code : core::languages::readerLanguages()) m_language->addItem(core::languages::nativeName(code), code);
    m_language->setCurrentIndex(qMax(0, m_language->findData(settings.readerLanguage)));
    langRow->addWidget(m_language);
    langRow->addWidget(muted(tr("Translations and explanations are given in it. More languages come as their packs are made "
                                "complete.")), 1);
    lay->addLayout(langRow);

    lay->addWidget(section(tr("LANGUAGES TO READ")));
    auto* packsBox = new QWidget;
    m_packs = new QVBoxLayout(packsBox);
    m_packs->setContentsMargins(0, 0, 0, 0);
    m_packs->setSpacing(8);
    lay->addWidget(packsBox);
    lay->addWidget(muted(tr("Gemini (online, free) gives the best answers: add a key in Settings > Translation. Packs can be "
                            "downloaded or removed later in Settings > General.")));
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

    connect(m_language, &QComboBox::currentIndexChanged, this, [this] { fillPacks(); });
    connect(m_start, &QPushButton::clicked, this, [this] { finish(true); });
    connect(m_later, &QPushButton::clicked, this, [this] { finish(false); });
    fillPacks();
    resize(560, sizeHint().height());
}

QString WelcomeDialog::readerLanguage() const { return m_language->currentData().toString(); }

void WelcomeDialog::setReaderLanguage(const QString& code)
{
    const int i = m_language->findData(code);
    if (i >= 0) m_language->setCurrentIndex(i);
}

void WelcomeDialog::fillPacks()
{
    while (QLayoutItem* it = m_packs->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    m_rows.clear();
    const QLocale loc;
    for (const core::languages::LanguagePack& p : core::languages::packsFor(readerLanguage())) {
        Row r;
        r.language = p.language;
        r.installed = m_installed && m_installed(p.language);
        auto* holder = new QWidget;
        auto* v = new QVBoxLayout(holder);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(1);
        const QString name = p.language == p.reader ? tr("%1 (Definitions Only)").arg(core::languages::englishName(p.language))
                                                    : core::languages::englishName(p.language);
        r.box = new QCheckBox(name);
        r.box->setProperty("testId", QStringLiteral("welcomePack-") + p.language);
        // Installed already: ticked, nothing to download.
        r.box->setChecked(r.installed || m_settings.practiceLanguages.contains(p.language));
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
        if (const auto p = core::languages::packFor(readerLanguage(), l)) bytes += p->downloadSize();
    m_start->setText(bytes > 0 ? tr("Download and Start (%1 MB)").arg(QLocale().toString(double(bytes) / 1e6, 'f', 0)) : tr("Start Reading"));
}

void WelcomeDialog::finish(bool download)
{
    // Asked once: "Not Now" too (everything is in Settings > General).
    m_settings.welcomeShown = true;
    if (download) {
        m_settings.readerLanguage = readerLanguage();
        core::languages::setReaderLanguage(m_settings.readerLanguage);
        for (const Row& r : m_rows)
            if (r.box->isChecked() && !m_settings.practiceLanguages.contains(r.language)) m_settings.practiceLanguages << r.language;
        const QStringList get = chosen();
        accept();
        if (!get.isEmpty()) emit downloadRequested(get);
    } else {
        reject();
    }
}

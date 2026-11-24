#include "BilingualPanel.h"

#include "core/translate/LanguagePacks.h"

#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

BilingualPanel::BilingualPanel(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Panel"));
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral("#Panel { border-right: 0; }"));
    setProperty("testId", "bilingualPanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 8);
    lay->setSpacing(4);
    // The reader's language ("ENGLISH"; "ESPAÑOL" for a Spanish reader).
    auto* title = new QLabel(core::languages::displayName(core::languages::readerLanguage()).toUpper());
    title->setObjectName(QStringLiteral("PanelTitle"));
    lay->addWidget(title);
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("Muted"));
    m_status->setWordWrap(true);
    m_status->setContentsMargins(16, 0, 16, 4);
    m_status->setProperty("testId", "bilingualStatus");
    m_status->hide();
    lay->addWidget(m_status);
    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list = new QWidget;
    m_list->setObjectName(QStringLiteral("Panel"));
    m_listLay = new QVBoxLayout(m_list);
    m_listLay->setContentsMargins(16, 4, 16, 8);
    m_listLay->setSpacing(12);
    m_listLay->addStretch(1);
    m_scroll->setWidget(m_list);
    lay->addWidget(m_scroll, 1);
}

void BilingualPanel::setTheme(const Theme& t)
{
    m_theme = t;
    setParagraphs(m_english, m_status->text());
}

QString BilingualPanel::status() const { return m_status->isHidden() ? QString() : m_status->text(); }

void BilingualPanel::setParagraphs(const QStringList& english, const QString& status)
{
    m_english = english;
    m_status->setText(status);
    m_status->setVisible(!status.isEmpty());
    while (m_listLay->count() > 1) {
        QLayoutItem* it = m_listLay->takeAt(0);
        delete it->widget();
        delete it;
    }
    for (int i = 0; i < english.size(); ++i) {
        auto* l = new QLabel(english[i].isEmpty() ? QStringLiteral("…") : english[i]);
        l->setWordWrap(true);
        l->setTextFormat(Qt::PlainText);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setProperty("testId", "bilingualParagraph");
        // The reading font, a little smaller than the page; paragraphs still on their way in muted ink.
        QFont f(QStringLiteral("Literata"));
        f.setPixelSize(15);
        l->setFont(f);
        if (english[i].isEmpty()) l->setObjectName(QStringLiteral("Muted"));
        m_listLay->insertWidget(i, l);
    }
}

void BilingualPanel::setMessage(const QString& message) { setParagraphs({}, message); }

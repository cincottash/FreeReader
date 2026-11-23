#include "DictionaryPopup.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtMath>

namespace {

QString roman(int n)
{
    static const char* const numerals[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"};
    return n >= 1 && n <= 12 ? QString::fromLatin1(numerals[n - 1]) : QString::number(n);
}

QString esc(const QString& s) { return s.toHtmlEscaped(); }

// "Del latín tranquillum" -> "Del latín tranquillum."
QString sentence(QString s)
{
    s = s.trimmed();
    if (!s.isEmpty() && !QStringLiteral(".!?…").contains(s.back())) s += QLatin1Char('.');
    return s;
}

}  // namespace

DictionaryPopup::DictionaryPopup(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("DictCard"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::NoFocus);
    setProperty("testId", "dictionaryCard");
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(32);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(0, 0, 0, 90));
    setGraphicsEffect(shadow);

    m_lay = new QVBoxLayout(this);
    m_lay->setContentsMargins(20, 14, 20, 12);
    m_lay->setSpacing(6);

    m_title = new QLabel;
    m_title->setProperty("testId", "dcTitle");
    m_title->setWordWrap(true);
    m_lay->addWidget(m_title);

    m_body = new QLabel;
    m_body->setProperty("testId", "dcBody");
    m_body->setWordWrap(true);
    m_body->setTextFormat(Qt::RichText);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_body->setFocusPolicy(Qt::NoFocus);
    m_body->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_scroll = new QScrollArea;
    m_scroll->setWidget(m_body);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_body->setAutoFillBackground(false);
    m_lay->addWidget(m_scroll, 1);

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 4, 0, 0);
    auto link = [](const QString& text, const char* id) {
        auto* b = new QPushButton(text);
        b->setObjectName(QStringLiteral("Link"));
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("testId", id);
        return b;
    };
    m_full = link(tr("Full Definition"), "dcFull");
    m_full->setToolTip(tr("The whole entry on Wiktionary, in your browser"));
    m_translate = link(tr("Translate"), "dcTranslate");
    m_translate->setToolTip(tr("The translation pop-up for this word"));
    m_getDefinitions = link(tr("Get Definitions…"), "dcGetDefinitions");
    m_getDefinitions->setToolTip(tr("A dictionary of definitions in the book's language is an optional download in Settings"));
    actions->addWidget(m_full);
    actions->addWidget(m_translate);
    actions->addWidget(m_getDefinitions);
    actions->addStretch(1);
    m_lay->addLayout(actions);
    m_source = new QLabel;
    m_source->setObjectName(QStringLiteral("Muted"));
    m_source->setProperty("testId", "dcSource");
    m_source->setWordWrap(true);
    m_source->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_lay->addWidget(m_source);

    connect(m_full, &QPushButton::clicked, this, [this] { emit fullDefinitionRequested(m_url); });
    connect(m_translate, &QPushButton::clicked, this, [this] {
        hide();
        emit translateRequested();
    });
    connect(m_getDefinitions, &QPushButton::clicked, this, [this] {
        hide();
        emit settingsRequested();
    });
    hide();
}

void DictionaryPopup::setTheme(const Theme& t)
{
    m_theme = t;
    // The page's own colors, as a dictionary page beside the book.
    setStyleSheet(QStringLiteral("#DictCard { background: %1; border: 1px solid %2; border-radius: 10px; }")
                      .arg(t.bg.name(), t.line.name()));
    m_title->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; letter-spacing: 1px;").arg(t.muted.name()));
    if (!m_card.isEmpty()) {
        m_html = toHtml(m_card, m_theme);
        m_body->setText(m_html);
    }
}

QString DictionaryPopup::toHtml(const core::DictionaryCard& card, const Theme& t)
{
    const QString muted = t.muted.name();
    QString h = QStringLiteral("<div style=\"font-family: 'Literata', Georgia, serif; font-size: 15px; color: %1;\">").arg(t.fg.name());
    for (int i = 0; i < card.headwords.size(); ++i) {
        const core::DictionaryHeadword& w = card.headwords[i];
        if (i) h += QStringLiteral("<hr style=\"color: %1;\"/>").arg(t.line.name());
        QString ipa;
        for (const core::DictionaryEntry& e : w.entries)
            if (!e.pronunciation.isEmpty()) {
                ipa = e.pronunciation;
                break;
            }
        h += QStringLiteral("<p style=\"margin: 2px 0 2px 0;\"><span style=\"font-size: 21px; font-weight: 600;\">%1</span>").arg(esc(w.word));
        if (!ipa.isEmpty()) h += QStringLiteral("&nbsp;&nbsp;<span style=\"color: %1;\">%2</span>").arg(muted, esc(ipa));
        h += QStringLiteral("</p>");
        if (!w.formOf.isEmpty())
            h += QStringLiteral("<p style=\"margin: 0 0 4px 0; color: %1;\"><i>%2</i>: %3</p>")
                     .arg(muted, esc(w.formOf), w.formNote.isEmpty() ? QObject::tr("a form of “%1”.").arg(esc(w.word)) : esc(sentence(w.formNote)));
        // Forms and origin once when every part of speech shares them (as a printed dictionary puts them first).
        QString forms, origin;
        bool sameOrigin = true;
        for (const core::DictionaryEntry& e : w.entries) {
            if (forms.isEmpty()) forms = e.forms;
            if (origin.isEmpty()) origin = e.origin;
            else if (!e.origin.isEmpty() && e.origin != origin) sameOrigin = false;
        }
        if (forms.contains(QLatin1Char(','))) h += QStringLiteral("<p style=\"margin: 2px 0;\">%1.").arg(esc(forms));
        else if (sameOrigin && !origin.isEmpty()) h += QStringLiteral("<p style=\"margin: 2px 0;\">");
        if (sameOrigin && !origin.isEmpty()) h += QStringLiteral(" <span style=\"color: %1;\">%2</span>").arg(muted, esc(sentence(origin)));
        if (forms.contains(QLatin1Char(',')) || (sameOrigin && !origin.isEmpty())) h += QStringLiteral("</p>");

        const bool numbered = w.entries.size() > 1;
        for (int j = 0; j < w.entries.size(); ++j) {
            const core::DictionaryEntry& e = w.entries[j];
            h += QStringLiteral("<p style=\"margin: 8px 0 2px 0;\">");
            if (numbered) h += QStringLiteral("<b>%1.</b> ").arg(roman(j + 1));
            h += QStringLiteral("<i>%1</i>").arg(esc(e.partOfSpeech));
            if (!sameOrigin && !e.origin.isEmpty()) h += QStringLiteral(" <span style=\"color: %1;\">%2</span>").arg(muted, esc(sentence(e.origin)));
            h += QStringLiteral("</p>");
            for (int k = 0; k < e.senses.size(); ++k) {
                const core::DictionarySense& s = e.senses[k];
                h += QStringLiteral("<p style=\"margin: 3px 0 3px 14px;\">");
                if (e.senses.size() > 1) h += QStringLiteral("<b>%1.</b> ").arg(k + 1);
                if (!s.labels.isEmpty()) h += QStringLiteral("<i style=\"color: %1;\">%2.</i> ").arg(muted, esc(s.labels.join(QStringLiteral(", "))));
                h += esc(s.gloss);
                for (const QString& x : s.examples)
                    h += QStringLiteral("<br/><span style=\"color: %1;\"><i>“%2”</i></span>").arg(muted, esc(x));
                h += QStringLiteral("</p>");
            }
        }
    }
    h += QStringLiteral("</div>");
    return h;
}

void DictionaryPopup::showCard(const core::DictionaryCard& card, const QString& title, const QString& source,
                               const QUrl& fullDefinition, const QRect& anchor, bool offerDefinitions)
{
    m_card = card;
    m_url = fullDefinition;
    m_anchor = anchor;
    m_title->setText(title.toUpper());
    m_html = toHtml(card, m_theme);
    m_body->setText(m_html);
    m_full->setVisible(m_url.isValid());
    m_getDefinitions->setVisible(offerDefinitions);
    m_source->setText(source);
    m_source->setVisible(!source.isEmpty());
    m_scroll->verticalScrollBar()->setValue(0);
    place();
}

void DictionaryPopup::place()
{
    QWidget* p = parentWidget();
    if (!p) return;
    const int w = qMin(460, p->width() - 32);
    setFixedWidth(w);
    // As tall as the entry needs, up to two thirds of the page (then it scrolls).
    const int bodyWidth = w - 40 - m_scroll->verticalScrollBar()->sizeHint().width();
    // The label's own estimate runs short for rich text: lay the text out as the label will.
    QTextDocument doc;
    doc.setDefaultFont(m_body->font());
    doc.setDocumentMargin(0);
    doc.setHtml(m_html);
    doc.setTextWidth(bodyWidth);
    const int bodyHeight = qCeil(doc.size().height()) + 8;
    m_lay->activate();
    const int chrome = m_lay->contentsMargins().top() + m_lay->contentsMargins().bottom() + m_title->sizeHint().height() +
                       m_full->sizeHint().height() + (m_source->isVisible() ? m_source->heightForWidth(w - 40) : 0) + 4 * m_lay->spacing() + 8;
    const int h = qMin(qMax(160, p->height() * 2 / 3), chrome + bodyHeight + 4);
    setFixedHeight(qMax(120, qMin(h, p->height() - 32)));
    int x = qBound(16, m_anchor.center().x() - w / 2, qMax(16, p->width() - w - 16));
    int y = m_anchor.bottom() + 10;
    if (y + height() > p->height() - 16) y = m_anchor.top() - height() - 10;
    if (y < 16) y = qMax(16, p->height() - height() - 16);
    move(x, y);
    show();
    raise();
}

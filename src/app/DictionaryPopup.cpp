#include "DictionaryPopup.h"

#include "core/translate/DictionaryCardHtml.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtMath>


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
    return core::dictionaryCardHtml(card, t.fg, t.muted, t.line);
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
    const int cap = qMin(qMax(160, p->height() * 2 / 3), p->height() - 32);
    int h = qMax(120, qMin(cap, chrome + bodyHeight + 4 + 2 * frameWidth()));
    setFixedHeight(h);
    // As really laid out: while the entry still scrolls and there is room, the card grows by what is hidden.
    for (int pass = 0; pass < 3 && h < cap; ++pass) {
        m_lay->activate();
        m_scroll->widget()->adjustSize();
        const int hidden = m_scroll->verticalScrollBar()->maximum();
        int missing = hidden;
        for (QLabel* l : {m_title, m_source})
            if (l->isVisibleTo(this)) missing += qMax(0, l->heightForWidth(l->width()) - l->height());
        if (missing <= 0) break;
        h = qMin(cap, h + missing);
        setFixedHeight(h);
    }
    int x = qBound(16, m_anchor.center().x() - w / 2, qMax(16, p->width() - w - 16));
    int y = m_anchor.bottom() + 10;
    if (y + height() > p->height() - 16) y = m_anchor.top() - height() - 10;
    if (y < 16) y = qMax(16, p->height() - height() - 16);
    move(x, y);
    show();
    raise();
}

#include "DictionaryPopup.h"

#include "FlipAnimation.h"
#include "Icons.h"

#include <QLocale>

#include "core/translate/DictionaryCardHtml.h"
#include "core/translate/LanguagePacks.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QHash>
#include <QScrollBar>
#include <QTextDocument>
#include <QToolButton>
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

    // The title, and Highlight at the top right (the owner's request, 2026-11-24: as in the translation pop-up).
    auto* head = new QHBoxLayout;
    head->setSpacing(10);
    m_title = new QLabel;
    m_title->setProperty("testId", "dcTitle");
    m_title->setWordWrap(true);
    head->addWidget(m_title, 1);
    m_highlight = new QPushButton(tr("Highlight"));
    m_highlight->setFocusPolicy(Qt::NoFocus);
    m_highlight->setCursor(Qt::PointingHandCursor);
    m_highlight->setProperty("testId", "dcHighlight");
    m_highlight->setFixedHeight(30);
    m_highlight->setStyleSheet(QStringLiteral("QPushButton { padding: 2px 10px; font-size: 12px; }"));
    m_highlight->hide();
    head->addWidget(m_highlight, 0, Qt::AlignTop);
    // Hear Pronunciation beside Highlight, as in the translation pop-up (the word's card took its place for words).
    m_speak = new QToolButton;
    m_speak->setObjectName(QStringLiteral("Icon"));
    m_speak->setFixedSize(32, 32);
    m_speak->setIconSize(QSize(18, 18));
    m_speak->setToolTip(tr("Hear Pronunciation"));
    m_speak->setCursor(Qt::PointingHandCursor);
    m_speak->setFocusPolicy(Qt::NoFocus);
    m_speak->setProperty("testId", "dcSpeak");
    m_speak->hide();
    head->addWidget(m_speak, 0, Qt::AlignTop);
    m_lay->addLayout(head);
    m_speechNote = new QLabel;
    m_speechNote->setObjectName(QStringLiteral("Muted"));
    m_speechNote->setProperty("testId", "dcSpeechNote");
    m_speechNote->setWordWrap(true);
    m_speechNote->setStyleSheet(QStringLiteral("font-size: 12px;"));
    m_speechNote->hide();
    m_lay->addWidget(m_speechNote);
    m_speechSettings = new QPushButton(tr("Open Speech Settings"));
    m_speechSettings->setObjectName(QStringLiteral("Link"));
    m_speechSettings->setFocusPolicy(Qt::NoFocus);
    m_speechSettings->setCursor(Qt::PointingHandCursor);
    m_speechSettings->setProperty("testId", "dcSpeechSettings");
    m_speechSettings->hide();
    m_lay->addWidget(m_speechSettings, 0, Qt::AlignLeft);
    connect(m_speechSettings, &QPushButton::clicked, this, &DictionaryPopup::speechSettingsRequested);
    connect(m_speak, &QToolButton::clicked, this, [this] { emit speakRequested(m_speakText, m_speakLanguage); });

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
    m_flip = new QPushButton;
    m_flip->setFocusPolicy(Qt::NoFocus);
    m_flip->setCursor(Qt::PointingHandCursor);
    m_flip->setProperty("testId", "dcFlip");
    m_flip->hide();
    actions->addWidget(m_flip);
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
    connect(m_highlight, &QPushButton::clicked, this, &DictionaryPopup::highlightRequested);
    connect(m_flip, &QPushButton::clicked, this, &DictionaryPopup::flipSide);
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
    m_speak->setIcon(icons::icon("speaker", t.fg));
    if (!m_card.isEmpty()) {
        m_html = toHtml(m_card, m_theme);
        m_body->setText(m_html);
    }
}

void DictionaryPopup::setSpeech(const QString& text, const QString& language)
{
    m_speakText = text.trimmed();
    m_speakLanguage = language;
#ifdef HAVE_TEXT_TO_SPEECH
    m_speak->setVisible(!m_speakText.isEmpty());
#endif
    m_speechNote->hide();
    m_speechSettings->hide();
}

void DictionaryPopup::showSpeechProblem(const QString& text, bool canOpenSettings)
{
    if (!isVisible()) return;
    m_speechNote->setText(text);
    m_speechNote->show();
    m_speechSettings->setVisible(canOpenSettings);
    place();
}

namespace {

// The part of speech as Gemini and LM Studio give it ("noun, masculine", "verb, preterite"), in Spanish for the
// Spanish side; empty when a word of it isn't known (never half English).
QString spanishPartOfSpeech(const QString& english)
{
    static const QHash<QString, QString> words = {
        {"noun", "sustantivo"}, {"verb", "verbo"}, {"adjective", "adjetivo"}, {"adverb", "adverbio"}, {"pronoun", "pronombre"},
        {"preposition", "preposición"}, {"conjunction", "conjunción"}, {"interjection", "interjección"}, {"article", "artículo"},
        {"determiner", "determinante"}, {"numeral", "numeral"}, {"masculine", "masculino"}, {"feminine", "femenino"},
        {"neuter", "neutro"}, {"plural", "plural"}, {"singular", "singular"}, {"preterite", "pretérito"}, {"imperfect", "imperfecto"},
        {"present", "presente"}, {"future", "futuro"}, {"conditional", "condicional"}, {"subjunctive", "subjuntivo"},
        {"indicative", "indicativo"}, {"imperative", "imperativo"}, {"infinitive", "infinitivo"}, {"participle", "participio"},
        {"gerund", "gerundio"}, {"past", "pasado"}, {"reflexive", "reflexivo"}, {"transitive", "transitivo"},
        {"intransitive", "intransitivo"}, {"pronominal", "pronominal"}, {"diminutive", "diminutivo"}, {"augmentative", "aumentativo"},
        {"proper", "propio"}, {"phrase", "locución"}, {"idiom", "modismo"}, {"expression", "expresión"}, {"first", "primera"},
        {"second", "segunda"}, {"third", "tercera"}, {"person", "persona"}, {"tense", "tiempo"}, {"form", "forma"},
        {"perfect", "perfecto"}, {"pluperfect", "pluscuamperfecto"}, {"simple", "simple"}, {"and", "y"}, {"or", "o"}};
    const QString rest = english.toLower();
    QString result;
    int i = 0;
    while (i < rest.size()) {
        if (rest[i].isLetter() || rest[i] == QLatin1Char('-')) {
            int j = i;
            while (j < rest.size() && (rest[j].isLetter() || rest[j] == QLatin1Char('-'))) ++j;
            const auto it = words.constFind(rest.mid(i, j - i));
            if (it == words.constEnd()) return {};
            result += it.value();
            i = j;
        } else {
            result += rest[i];
            ++i;
        }
    }
    return result.trimmed();
}

}  // namespace

core::DictionaryCard DictionaryPopup::cardFromResult(const core::TranslationResult& r, bool bookSide, const QString& bookLanguage)
{
    core::DictionaryCard c;
    c.text = r.text.trimmed();
    core::DictionaryHeadword w;
    w.word = r.lemma.isEmpty() ? c.text : r.lemma;
    if (!r.lemma.isEmpty()) w.formOf = c.text;   // "a form of “tener”."
    core::DictionaryEntry e;
    if (bookSide) {
        if (r.definition.trimmed().isEmpty()) return {};
        // The part of speech comes in the reader's language: put into Spanish for a Spanish book, else left out.
        e.partOfSpeech = bookLanguage == QLatin1String("es") ? spanishPartOfSpeech(r.partOfSpeech) : QString();
        if (!r.lemma.isEmpty()) {
            w.formOf.clear();   // the form's name in the book's language isn't known: the headword alone
            if (bookLanguage == QLatin1String("es") || bookLanguage == QLatin1String("en")) {
                w.formOf = c.text;
                w.formNote = bookLanguage == QLatin1String("es") ? QStringLiteral("forma de %1").arg(r.lemma)
                                                                 : QStringLiteral("a form of %1").arg(r.lemma);
            }
        }
        e.senses << core::DictionarySense{r.definition.trimmed(), {}, {}};
    } else {
        if (r.translation.trimmed().isEmpty()) return {};
        e.partOfSpeech = r.partOfSpeech;
        e.senses << core::DictionarySense{r.translation.trimmed(), {}, {}};
        for (const QString& a : r.alternatives) e.senses << core::DictionarySense{a, {}, {}};
    }
    w.entries << e;
    c.headwords << w;
    return c;
}

QString DictionaryPopup::toHtml(const core::DictionaryCard& card, const Theme& t)
{
    return core::dictionaryCardHtml(card, t.fg, t.muted, t.line);
}

void DictionaryPopup::showCard(const core::DictionaryCard& card, const QString& title, const QString& source,
                               const QUrl& fullDefinition, const QRect& anchor, bool offerDefinitions)
{
    m_twoSides = false;
    m_flip->hide();
    m_card = card;
    m_url = fullDefinition;
    m_anchor = anchor;
    m_title->setText(title.toUpper());
    m_html = toHtml(card, m_theme);
    m_body->setText(m_html);
    m_full->setVisible(m_url.isValid());
    m_translate->setVisible(m_translateLink);
    m_getDefinitions->setVisible(offerDefinitions);
    m_source->setText(source);
    m_source->setVisible(!source.isEmpty());
    m_scroll->verticalScrollBar()->setValue(0);
    place();
}

void DictionaryPopup::showSides(const Side& front, const Side& back, const QRect& anchor)
{
    m_sides[0] = front;
    m_sides[1] = back;
    m_front = true;
    m_anchor = anchor;
    m_twoSides = true;
    showSide(m_sides[0]);
    m_scroll->verticalScrollBar()->setValue(0);
    place();
}

void DictionaryPopup::showSide(const Side& s)
{
    m_card = s.card;
    m_url = s.fullDefinition;
    m_title->setText(s.title.toUpper());
    // A side with nothing on it says why (and offers the download where there is one).
    m_html = s.card.isEmpty() ? QStringLiteral("<p style=\"color: %1;\">%2</p>").arg(m_theme.muted.name(), s.missing.toHtmlEscaped())
                              : toHtml(s.card, m_theme);
    m_body->setText(m_html);
    m_full->setVisible(m_url.isValid());
    m_translate->setVisible(m_translateLink);
    m_getDefinitions->setVisible(s.card.isEmpty() && s.offerDefinitions);
    m_source->setText(s.source);
    m_source->setVisible(!s.source.isEmpty());
    // The button names the other side's language, in the reader's language.
    const Side& other = m_sides[m_front ? 1 : 0];
    const QString name = core::languages::displayName(other.language);
    m_flip->setText(tr("Flip to %1").arg(name));
    m_flip->setToolTip(tr("Turn the card over: the %1").arg(other.title));
    m_flip->show();
}

void DictionaryPopup::flipSide()
{
    if (!m_twoSides) return;
    flip::turn(this, [this] {
        m_front = !m_front;
        showSide(m_sides[m_front ? 0 : 1]);
        m_scroll->verticalScrollBar()->setValue(0);
        place();
    }, m_theme.bg, 10);
}

void DictionaryPopup::setHighlight(bool available, bool highlighted, const QColor& color)
{
    m_highlight->setText(highlighted ? tr("Remove Highlight") : tr("Highlight"));
    m_highlight->setIcon(icons::dot(color, highlighted ? m_theme.fg : QColor()));
    m_highlight->setToolTip(highlighted ? tr("Take the highlight away") : tr("Highlight this word in the color used last"));
    m_highlight->setVisible(available);
}

void DictionaryPopup::place()
{
    QWidget* p = parentWidget();
    if (!p) return;
    const int w = qMin(460, p->width() - 32);
    setFixedWidth(w);
    // Shown before it is measured: a hidden widget's new size reaches its children only once shown, so the first
    // card's measures were stale (too tall, then cut short, 2026-11-25). Nothing is painted before it is moved.
    if (!isVisible()) show();
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
                       m_full->sizeHint().height() + (m_source->isVisible() ? m_source->heightForWidth(w - 40) : 0) + 4 * m_lay->spacing() + 8 +
                       (m_speechNote->isVisibleTo(this) ? m_speechNote->heightForWidth(w - 40) + m_lay->spacing() : 0) +
                       (m_speechSettings->isVisibleTo(this) ? m_speechSettings->sizeHint().height() + m_lay->spacing() : 0);
    const int cap = qMin(qMax(160, p->height() * 2 / 3), p->height() - 32);
    int h = qMax(120, qMin(cap, chrome + bodyHeight + 4 + 2 * frameWidth()));
    setFixedHeight(h);
    // The estimate of the rest can run long (the first card shown had a hand's width of empty space and covered the
    // word, 2026-11-25): as really laid out, the entry gets what it needs and no more.
    m_lay->activate();
    if (const int slack = m_scroll->viewport()->height() - bodyHeight; slack > 16) {
        h = qMax(120, h - slack + 12);   // a little to spare: the label lays rich text out a few pixels taller
        setFixedHeight(h);
    }
    // As really laid out: while the entry still scrolls and there is room, the card grows by what is hidden.
    for (int pass = 0; pass < 3 && h < cap; ++pass) {
        m_lay->activate();
        m_scroll->widget()->adjustSize();
        const int hidden = m_scroll->verticalScrollBar()->maximum();
        int missing = hidden;
        for (QLabel* l : {m_title, m_source, m_speechNote})
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

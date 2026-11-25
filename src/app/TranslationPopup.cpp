#include "TranslationPopup.h"

#include "FlipAnimation.h"
#include "Icons.h"

#include <QApplication>
#include <QHash>
#include <QClipboard>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QString languageTitle(const QString& code)
{
    if (code.isEmpty()) return QObject::tr("DEFINITION");
    const QLocale l(code);
    QString name = l.nativeLanguageName();
    // "español de España", "English (United States)": the language alone.
    const QString territory = l.nativeTerritoryName();
    if (name.contains(QLatin1Char('('))) name = name.left(name.indexOf(QLatin1Char('('))).trimmed();
    if (!territory.isEmpty() && name.endsWith(territory, Qt::CaseInsensitive)) {
        name.chop(territory.size());
        name = name.trimmed();
        for (const char* joiner : {" de", " do", " da", " di", " du", " von", " of"})
            if (name.endsWith(QLatin1String(joiner))) name.chop(int(qstrlen(joiner)));
    }
    if (name.trimmed().isEmpty()) name = QLocale::languageToString(l.language());
    return name.trimmed().toUpper();
}

}  // namespace

TranslationPopup::TranslationPopup(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Pop"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::NoFocus);
    setProperty("testId", "translationPopup");
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(32);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(0, 0, 0, 90));
    setGraphicsEffect(shadow);

    m_lay = new QVBoxLayout(this);
    m_lay->setContentsMargins(18, 14, 18, 12);
    m_lay->setSpacing(6);

    auto* head = new QHBoxLayout;
    head->setSpacing(10);
    m_word = label("tpWord");
    QFont wf(QStringLiteral("Literata"));
    wf.setPixelSize(22);
    wf.setWeight(QFont::DemiBold);
    m_word->setFont(wf);
    m_detail = label("tpDetail", "Muted");
    m_detail->setStyleSheet(QStringLiteral("font-size: 12px;"));
    m_speak = new QToolButton;
    m_speak->setObjectName(QStringLiteral("Icon"));
    m_speak->setFixedSize(32, 32);
    m_speak->setIconSize(QSize(18, 18));
    m_speak->setToolTip(tr("Hear Pronunciation"));
    m_speak->setCursor(Qt::PointingHandCursor);
    m_speak->setFocusPolicy(Qt::NoFocus);
    m_speak->setProperty("testId", "tpSpeak");
#ifndef HAVE_TEXT_TO_SPEECH
    m_speak->hide();
#endif
    // Highlight at the top right, beside Hear Pronunciation (the owner's request, 2026-11-24).
    m_highlight = new QPushButton(tr("Highlight"));
    m_highlight->setFocusPolicy(Qt::NoFocus);
    m_highlight->setCursor(Qt::PointingHandCursor);
    m_highlight->setProperty("testId", "tpHighlight");
    m_highlight->setFixedHeight(30);
    m_highlight->setStyleSheet(QStringLiteral("QPushButton { padding: 2px 10px; font-size: 12px; }"));
    m_highlight->hide();
    head->addWidget(m_word, 0, Qt::AlignBaseline);
    head->addWidget(m_detail, 1, Qt::AlignBaseline);
    head->addWidget(m_highlight, 0, Qt::AlignTop);
    head->addWidget(m_speak, 0, Qt::AlignTop);
    m_lay->addLayout(head);
    m_speechNote = label("tpSpeechNote", "Muted");
    m_speechNote->setStyleSheet(QStringLiteral("font-size: 12px;"));
    m_speechNote->hide();
    m_lay->addWidget(m_speechNote);
    m_speechSettings = new QPushButton(tr("Open Speech Settings"));
    m_speechSettings->setObjectName(QStringLiteral("Link"));
    m_speechSettings->setFocusPolicy(Qt::NoFocus);
    m_speechSettings->setCursor(Qt::PointingHandCursor);
    m_speechSettings->setProperty("testId", "tpSpeechSettings");
    m_speechSettings->hide();
    m_lay->addWidget(m_speechSettings, 0, Qt::AlignLeft);
    connect(m_speechSettings, &QPushButton::clicked, this, &TranslationPopup::speechSettingsRequested);

    m_translation = label("tpTranslation");
    m_translation->setStyleSheet(QStringLiteral("font-size: 17px;"));
    m_lay->addWidget(m_translation);
    m_alternatives = label("tpAlternatives", "Muted");
    m_lay->addWidget(m_alternatives);
    m_note = label("tpNote", "Muted");
    m_note->setStyleSheet(QStringLiteral("font-size: 13px;"));
    m_lay->addWidget(m_note);

    m_rule = new QFrame;
    m_rule->setFixedHeight(1);
    m_rule->setObjectName(QStringLiteral("Rule"));
    m_lay->addSpacing(2);
    m_lay->addWidget(m_rule);
    m_lay->addSpacing(2);

    auto title = [this](const char* id) {
        QLabel* l = label(id, "PanelTitle");
        l->setStyleSheet(QStringLiteral("padding: 4px 0 0 0;"));
        return l;
    };
    m_definitionTitle = title("tpDefinitionTitle");
    m_lay->addWidget(m_definitionTitle);
    m_definition = label("tpDefinition");
    m_lay->addWidget(m_definition);
    // No "in this sentence": the whole sentence translated is Explain This Sentence's job (the owner's request,
    // 2026-11-25). The answer still has it, for the Word List and Practice.

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 6, 0, 0);
    m_copy = new QPushButton(tr("Copy"));
    m_copy->setFocusPolicy(Qt::NoFocus);
    m_copy->setCursor(Qt::PointingHandCursor);
    m_copy->setToolTip(tr("Copy the translation"));
    m_copy->setProperty("testId", "tpCopy");
    m_settings = new QPushButton(tr("Translation Settings…"));
    m_settings->setObjectName(QStringLiteral("Link"));
    m_settings->setFocusPolicy(Qt::NoFocus);
    m_settings->setCursor(Qt::PointingHandCursor);
    m_settings->setProperty("testId", "tpSettings");
    // Flip (the Dictionary's two sides): at the bottom right (the owner's sketch, 2026-11-23).
    m_flip = new QPushButton;
    m_flip->setFocusPolicy(Qt::NoFocus);
    m_flip->setCursor(Qt::PointingHandCursor);
    m_flip->setProperty("testId", "tpFlip");
    m_flip->hide();
    actions->addWidget(m_copy);
    actions->addWidget(m_settings);
    actions->addStretch(1);
    actions->addWidget(m_flip);
    m_lay->addLayout(actions);
    m_source = label("tpSource", "Muted");
    m_source->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_lay->addWidget(m_source);

    connect(m_copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(copyText());
        m_copy->setText(tr("Copied"));
    });
    connect(m_flip, &QPushButton::clicked, this, &TranslationPopup::flipSide);
    connect(m_settings, &QPushButton::clicked, this, &TranslationPopup::settingsRequested);
    connect(m_highlight, &QPushButton::clicked, this, &TranslationPopup::highlightRequested);
    connect(m_speak, &QToolButton::clicked, this, [this] { emit speakRequested(m_text, m_language); });
    hide();
}

QLabel* TranslationPopup::label(const char* testId, const char* object)
{
    auto* l = new QLabel;
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setFocusPolicy(Qt::NoFocus);
    l->setProperty("testId", testId);
    if (object) l->setObjectName(QString::fromLatin1(object));
    return l;
}

void TranslationPopup::setTheme(const Theme& t)
{
    m_theme = t;
    m_speak->setIcon(icons::icon("speaker", t.fg));
    m_rule->setStyleSheet(QStringLiteral("background: %1;").arg(t.line.name()));
}

void TranslationPopup::setHead(const QString& word, const QString& detail)
{
    m_word->setText(word);
    m_word->setVisible(!word.isEmpty());
    m_detail->setText(detail);
    m_detail->setVisible(!detail.isEmpty());
}

void TranslationPopup::showLoading(const QString& text, const QString& language, const QRect& anchor)
{
    m_text = text.trimmed();
    m_language = language;
    m_anchor = anchor;
    m_result = {};
    m_loading = true;
    const bool word = !m_text.contains(QLatin1Char(' ')) && m_text.size() <= 40;
    setHead(word ? m_text : QString(), QString());
    m_speak->setVisible(false);
    m_speechNote->hide();
    m_speechSettings->hide();
    m_translation->setText(tr("Translating…"));
    m_translation->setObjectName(QStringLiteral("Muted"));
    m_translation->style()->polish(m_translation);
    for (QWidget* w : {static_cast<QWidget*>(m_alternatives), static_cast<QWidget*>(m_note), static_cast<QWidget*>(m_rule),
                       static_cast<QWidget*>(m_definitionTitle), static_cast<QWidget*>(m_definition),
                       static_cast<QWidget*>(m_copy), static_cast<QWidget*>(m_settings),
                       static_cast<QWidget*>(m_highlight), static_cast<QWidget*>(m_flip)})
        w->hide();
    m_canHighlight = false;   // the reader says again for these words
    m_twoSides = false;
    m_source->setText(QString());
    m_source->hide();
    place();
}

void TranslationPopup::showAsking(const QString& provider)
{
    if (!m_loading || !isVisible()) return;   // closed meanwhile (page turned, Esc): stays closed
    m_source->setText(tr("Asking %1…").arg(provider));
    m_source->show();
    place();
}

void TranslationPopup::showResult(const core::TranslationResult& r, bool dictionaryFirst)
{
    m_loading = false;
    m_result = r;
    // Two sides when there is a definition in the book's language, for a book not in the reader's language: the
    // translation all in the reader's language (no word of the book's: the owner's request, 2026-11-25), the
    // definition behind Flip. Dictionary starts on the definition, Translate on the translation.
    const QString lang = m_language.section(QLatin1Char('-'), 0, 0).toLower();
    m_twoSides = !r.definition.trimmed().isEmpty() && !lang.isEmpty() && lang != readerLanguage();
    m_bookSide = dictionaryFirst;
    m_flip->setVisible(m_twoSides);
    const bool word = !m_text.contains(QLatin1Char(' ')) && m_text.size() <= 40;
    QString detail = r.partOfSpeech;
    if (!r.lemma.isEmpty()) detail += (detail.isEmpty() ? QString() : QStringLiteral(" · ")) + tr("from %1").arg(r.lemma);
    setHead(word ? m_text : QString(), detail);
#ifdef HAVE_TEXT_TO_SPEECH
    m_speak->setVisible(true);
#endif
    m_translation->setObjectName(QString());
    m_translation->style()->polish(m_translation);
    m_translation->setText(r.translation);
    m_translation->setVisible(!r.translation.isEmpty());
    m_alternatives->setText(r.alternatives.isEmpty() ? QString() : tr("Also: %1").arg(r.alternatives.join(QStringLiteral(", "))));
    m_alternatives->setVisible(!r.alternatives.isEmpty());
    m_note->setText(r.note);
    m_note->setVisible(!r.note.isEmpty());

    m_definitionTitle->setText(languageTitle(m_language));
    m_definition->setText(r.definition);
    // Beside the translation only when it is in the reader's own language (a book in it); else behind Flip.
    const bool showDefinition = !r.definition.isEmpty() && !m_twoSides;
    m_definitionTitle->setVisible(showDefinition);
    m_definition->setVisible(showDefinition);
    m_rule->setVisible(showDefinition);
    if (dictionaryFirst && showDefinition) {
        // Dictionary: the definition comes right after the word.
        m_lay->removeWidget(m_definitionTitle);
        m_lay->removeWidget(m_definition);
        m_lay->insertWidget(1, m_definitionTitle);
        m_lay->insertWidget(2, m_definition);
    } else if (m_lay->indexOf(m_definitionTitle) < m_lay->indexOf(m_rule)) {
        m_lay->removeWidget(m_definitionTitle);
        m_lay->removeWidget(m_definition);
        const int at = m_lay->indexOf(m_rule) + 2;
        m_lay->insertWidget(at, m_definitionTitle);
        m_lay->insertWidget(at + 1, m_definition);
    }
    m_copy->setText(tr("Copy"));
    m_copy->setVisible(!r.translation.isEmpty());
    m_settings->hide();
    QString source = tr("Source: %1").arg(r.provider.isEmpty() ? tr("unknown") : r.provider);
    if (r.fromCache) source += tr(" (looked up before)");
    m_source->setText(source);
    m_source->show();
    if (m_twoSides) applySide();
    place();
}

void TranslationPopup::applySide()
{
    const core::TranslationResult& r = m_result;
    const QString lang = m_language.section(QLatin1Char('-'), 0, 0).toLower();
    const bool word = !m_text.contains(QLatin1Char(' ')) && m_text.size() <= 40;
    const QString bookName = core::languages::displayName(lang);
    const QString readerName = core::languages::displayName(readerLanguage());
    if (m_bookSide) {
        // All in the book's language: the words and their definition.
        setHead(word ? m_text : QString(), !r.lemma.isEmpty() && lang == QLatin1String("es")   ? QStringLiteral("de ") + r.lemma
                                           : !r.lemma.isEmpty() && lang == QLatin1String("en") ? QStringLiteral("from ") + r.lemma
                                                                                               : QString());
        for (QWidget* w : {static_cast<QWidget*>(m_translation), static_cast<QWidget*>(m_alternatives), static_cast<QWidget*>(m_note)}) w->hide();
        m_definitionTitle->setText(languageTitle(m_language));
        m_definitionTitle->show();
        m_definition->setText(r.definition);
        m_definition->show();
        m_rule->hide();
        m_flip->setText(tr("Flip to %1").arg(readerName));
        m_flip->setToolTip(tr("Turn the card over: everything in %1").arg(readerName));
    } else {
        // All in the reader's language: the meaning, other meanings, the note.
        QString detail = r.partOfSpeech;
        if (!r.lemma.isEmpty()) detail += (detail.isEmpty() ? QString() : QStringLiteral(" · ")) + tr("from %1").arg(r.lemma);
        setHead(word ? m_text : QString(), detail);
        m_translation->setText(r.translation);
        m_translation->setVisible(!r.translation.isEmpty());
        m_alternatives->setVisible(!r.alternatives.isEmpty());
        m_note->setVisible(!r.note.isEmpty());
        m_definitionTitle->hide();
        m_definition->hide();
        m_rule->hide();
        m_flip->setText(tr("Flip to %1").arg(bookName));
        m_flip->setToolTip(tr("Turn the card over: everything in %1").arg(bookName));
    }
    m_copy->setText(tr("Copy"));
    m_copy->setVisible(!copyText().isEmpty());
}

QString TranslationPopup::copyText() const
{
    if (m_twoSides && m_bookSide) return m_result.definition;
    return m_result.translation;
}

void TranslationPopup::flipSide()
{
    if (!m_twoSides || m_loading) return;
    flip::turn(this, [this] {
        m_bookSide = !m_bookSide;
        applySide();
        place();
    }, m_theme.pop, 14);
}

void TranslationPopup::showNothing(const QString& why)
{
    m_loading = false;
    m_result = {};
    m_twoSides = false;
    m_flip->hide();
    m_speak->setVisible(false);
    m_translation->setObjectName(QString());
    m_translation->style()->polish(m_translation);
    m_translation->setText(tr("No translation."));
    m_note->setText(why.isEmpty() ? tr("No translation service is set up.") : why);
    m_note->show();
    m_settings->show();
    m_source->hide();
    m_speechNote->hide();
    m_speechSettings->hide();
    place();
}

void TranslationPopup::showExplanation(const core::TranslationResult& r)
{
    m_loading = false;
    m_result = r;
    m_twoSides = false;
    m_flip->hide();
    setHead(QString(), QString());
    m_speak->setVisible(false);
    m_translation->setObjectName(QString());
    m_translation->style()->polish(m_translation);
    m_translation->setText(r.translation);
    m_translation->setVisible(!r.translation.isEmpty());
    m_alternatives->hide();
    m_note->setText(r.note);
    m_note->setVisible(!r.note.isEmpty());
    // The notes below the rule, where the definition goes.
    if (m_lay->indexOf(m_definitionTitle) < m_lay->indexOf(m_rule)) {
        m_lay->removeWidget(m_definitionTitle);
        m_lay->removeWidget(m_definition);
        const int at = m_lay->indexOf(m_rule) + 2;
        m_lay->insertWidget(at, m_definitionTitle);
        m_lay->insertWidget(at + 1, m_definition);
    }
    m_definitionTitle->setText(tr("HOW IT WORKS"));
    m_canHighlight = false;
    m_highlight->hide();
    QStringList notes;
    for (const QString& n : r.explanation) notes << QStringLiteral("• ") + n;
    m_definition->setText(notes.join(QLatin1Char('\n')));
    const bool hasNotes = !notes.isEmpty();
    m_definitionTitle->setVisible(hasNotes);
    m_definition->setVisible(hasNotes);
    m_rule->setVisible(hasNotes);
    m_copy->setText(tr("Copy"));
    m_copy->setVisible(!r.translation.isEmpty());
    m_settings->hide();
    QString source = tr("Source: %1").arg(r.provider.isEmpty() ? tr("unknown") : r.provider);
    if (r.fromCache) source += tr(" (explained before)");
    m_source->setText(source);
    m_source->show();
    place();
}

void TranslationPopup::setHighlight(bool available, bool highlighted, const QColor& color)
{
    m_canHighlight = available;
    m_highlight->setText(highlighted ? tr("Remove Highlight") : tr("Highlight"));
    m_highlight->setIcon(icons::dot(color, highlighted ? m_theme.fg : QColor()));
    m_highlight->setToolTip(highlighted ? tr("Take the highlight away") : tr("Highlight these words in the color used last"));
    const bool show = available && !m_loading;
    if (m_highlight->isVisible() == show) return;
    m_highlight->setVisible(show);
    if (isVisible()) place();
}

void TranslationPopup::showSpeechProblem(const QString& text, bool canOpenSettings)
{
    if (!isVisible()) return;
    m_speechNote->setText(text);
    m_speechNote->show();
    m_speechSettings->setVisible(canOpenSettings);
    place();
}

void TranslationPopup::place()
{
    QWidget* p = parentWidget();
    if (!p) return;
    int w = qMin(420, p->width() - 32);
    setFixedWidth(w);
    // A long answer in a short window: wider (up to 640 px) before it is cut at the window's bottom (an explanation
    // in a 640 px tall window lost its last line, 2026-11-24).
    {
        ensurePolished();
        for (QWidget* c : findChildren<QWidget*>()) c->ensurePolished();
        m_lay->invalidate();
        m_lay->activate();
        const int room = p->height() - 32;
        for (int wider = w + 110; m_lay->heightForWidth(w - 2 * frameWidth()) + 2 * frameWidth() > room && wider <= qMin(640, p->width() - 32);
             wider += 110) {
            w = wider;
            setFixedWidth(w);
            m_lay->invalidate();
            m_lay->activate();
        }
    }
    // Labels wrap to the width; ask the layout how tall that makes the pop-up. Their style sheets (font sizes) are
    // applied first: measured before that, a wrapped meaning came out a line short (the owner's report, 2026-11-23).
    ensurePolished();
    for (QWidget* c : findChildren<QWidget*>()) c->ensurePolished();
    m_lay->invalidate();
    m_lay->activate();
    // The border takes room too: the text has the inside of it, not the whole width.
    const int frame = 2 * frameWidth();
    int h = qMin(p->height() - 32, qMax(80, m_lay->heightForWidth(w - frame) + frame));
    setFixedHeight(h);
    // Then each wrapped label is checked as it was really laid out; the pop-up grows when one needs more room.
    for (int pass = 0; pass < 3 && h < p->height() - 32; ++pass) {
        m_lay->activate();
        int missing = 0;
        for (QLabel* l : findChildren<QLabel*>())
            if (l->isVisibleTo(this) && l->wordWrap()) missing += qMax(0, l->heightForWidth(l->width()) - l->height());
        if (missing == 0) break;
        h = qMin(p->height() - 32, h + missing);
        setFixedHeight(h);
    }
    int x = qBound(16, m_anchor.center().x() - w / 2, qMax(16, p->width() - w - 16));
    int y = m_anchor.bottom() + 10;
    if (y + h > p->height() - 16) y = m_anchor.top() - h - 10;
    if (y < 16) y = qMax(16, p->height() - h - 16);
    move(x, y);
    show();
    raise();
}

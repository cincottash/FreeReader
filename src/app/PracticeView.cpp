#include "PracticeView.h"

#include "Icons.h"
#include "Settings.h"

#include "core/practice/Answer.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

using core::practice::Rating;

namespace {

const char* kDirections[4] = {"recognize", "recall", "definition", "cloze"};

QLabel* wrapLabel(const char* testId, const char* object = nullptr)
{
    auto* l = new QLabel;
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setProperty("testId", testId);
    if (object) l->setObjectName(QString::fromLatin1(object));
    return l;
}

QLabel* sectionTitle(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("PanelTitle"));
    l->setStyleSheet(QStringLiteral("padding: 14px 0 4px 0;"));
    return l;
}

// The sentence as rich text with `word` in bold (first occurrence, any case).
QString withBold(const QString& sentence, const QString& word)
{
    const int at = word.isEmpty() ? -1 : int(sentence.indexOf(word, 0, Qt::CaseInsensitive));
    if (at < 0) return sentence.toHtmlEscaped();
    return sentence.left(at).toHtmlEscaped() + QStringLiteral("<b>") + sentence.mid(at, word.size()).toHtmlEscaped() + QStringLiteral("</b>") +
           sentence.mid(at + int(word.size())).toHtmlEscaped();
}

QString withGap(const QString& sentence, const QString& word)
{
    const int at = int(sentence.indexOf(word, 0, Qt::CaseInsensitive));
    if (at < 0) return sentence;
    return sentence.left(at) + QStringLiteral("_____") + sentence.mid(at + int(word.size()));
}

QString count(int n, const QString& one, const QString& many) { return QStringLiteral("%1 %2").arg(QLocale().toString(n), n == 1 ? one : many); }

}  // namespace

PracticeView::PracticeView(core::WordLog* log, Settings& settings, QWidget* parent) : QWidget(parent), m_log(log), m_settings(settings)
{
    setObjectName(QStringLiteral("Page"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::StrongFocus);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* bar = new QWidget;
    bar->setObjectName(QStringLiteral("Bar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 4, 12, 4);
    h->setSpacing(8);
    m_back = new QToolButton;
    m_back->setObjectName(QStringLiteral("Icon"));
    m_back->setFixedSize(40, 40);
    m_back->setIconSize(QSize(20, 20));
    m_back->setToolTip(tr("Back"));
    m_back->setCursor(Qt::PointingHandCursor);
    m_back->setFocusPolicy(Qt::NoFocus);
    m_back->setProperty("testId", "practiceBack");
    h->addWidget(m_back);
    m_heading = new QLabel(tr("Practice"));
    m_heading->setObjectName(QStringLiteral("Title"));
    h->addWidget(m_heading, 1);
    m_tabPractice = new QPushButton(tr("Practice"));
    m_tabList = new QPushButton(tr("Word List"));
    for (QPushButton* b : {m_tabPractice, m_tabList}) {
        b->setObjectName(QStringLiteral("Chip"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        h->addWidget(b);
    }
    m_tabPractice->setProperty("testId", "tabPractice");
    m_tabList->setProperty("testId", "tabWordList");
    lay->addWidget(bar);

    m_pages = new QStackedWidget;
    m_pages->addWidget(buildStart());
    m_pages->addWidget(buildCard());
    m_pages->addWidget(buildDone());
    m_pages->addWidget(buildList());
    lay->addWidget(m_pages, 1);

    connect(m_back, &QToolButton::clicked, this, [this] {
        if (page() == CardPage) endSession();
        emit backRequested();
    });
    connect(m_tabPractice, &QPushButton::clicked, this, [this] {
        if (page() != CardPage) showStart();
        m_tabPractice->setChecked(true);
    });
    connect(m_tabList, &QPushButton::clicked, this, [this] {
        if (page() == CardPage) endSession();
        showList();
    });
}

void PracticeView::setTheme(const Theme& t)
{
    m_theme = t;
    m_back->setIcon(icons::icon("back", t.fg));
}

PracticeView::Page PracticeView::page() const { return Page(m_pages->currentIndex()); }

QString PracticeView::languageName() const
{
    QHash<QString, int> n;
    for (const core::WordEntry& w : m_log->words()) ++n[w.language];
    QString best;
    int most = 0;
    for (auto it = n.cbegin(); it != n.cend(); ++it)
        if (it.value() > most && it.key() != QLatin1String("und")) {
            most = it.value();
            best = it.key();
        }
    if (best.isEmpty()) return tr("Book Language");
    return QLocale::languageToString(QLocale(best).language());
}

QString PracticeView::directionLabel(const QString& d) const
{
    const QString lang = languageName();
    if (d == QLatin1String("recognize")) return tr("%1 → English").arg(lang);
    if (d == QLatin1String("recall")) return tr("English → %1").arg(lang);
    if (d == QLatin1String("definition")) return tr("Definition → Word");
    return tr("Sentence With a Gap → Word");
}

// ---------------------------------------------------------------- Start page

QWidget* PracticeView::buildStart()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("Page"));
    scroll->setWidget(page);
    auto* outer = new QHBoxLayout(page);
    outer->addStretch(1);
    auto* col = new QWidget;
    col->setMaximumWidth(620);
    col->setMinimumWidth(320);
    outer->addWidget(col, 4);
    outer->addStretch(1);
    auto* lay = new QVBoxLayout(col);
    lay->setContentsMargins(16, 24, 16, 24);
    lay->setSpacing(6);

    auto* title = new QLabel(tr("Practice Your Words"));
    QFont tf = title->font();
    tf.setPixelSize(24);
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);
    lay->addWidget(title);
    m_counts = wrapLabel("practiceCounts", "Muted");
    lay->addWidget(m_counts);
    m_empty = wrapLabel("practiceEmpty");
    m_empty->setText(tr("No words yet. Select a word while reading to look it up: every lookup is saved here, ready to practise."));
    lay->addWidget(m_empty);

    lay->addWidget(sectionTitle(tr("WHAT TO PRACTISE")));
    auto* bookRow = new QHBoxLayout;
    bookRow->addWidget(new QLabel(tr("Words From")));
    m_book = new QComboBox;
    m_book->setProperty("testId", "practiceBook");
    bookRow->addWidget(m_book, 1);
    lay->addLayout(bookRow);
    for (int i = 0; i < 4; ++i) {
        m_dir[i] = new QCheckBox;
        m_dir[i]->setProperty("testId", (QStringLiteral("dir-") + QLatin1String(kDirections[i])).toLatin1().constData());
        m_dir[i]->setProperty("direction", QString::fromLatin1(kDirections[i]));
        lay->addWidget(m_dir[i]);
    }
    m_passages = new QCheckBox(tr("Include Passages (Longer Selections)"));
    m_passages->setProperty("testId", "practicePassages");
    lay->addWidget(m_passages);
    m_starred = new QCheckBox(tr("Starred Words Only"));
    m_starred->setProperty("testId", "practiceStarred");
    lay->addWidget(m_starred);

    lay->addWidget(sectionTitle(tr("HOW")));
    m_typed = new QCheckBox(tr("Type the Answer (Instead of Flipping the Card)"));
    m_typed->setProperty("testId", "practiceTyped");
    lay->addWidget(m_typed);
    m_sentenceFront = new QCheckBox(tr("Show the Sentence on the Front"));
    m_sentenceFront->setProperty("testId", "practiceSentenceFront");
    lay->addWidget(m_sentenceFront);
    auto* newRow = new QHBoxLayout;
    newRow->addWidget(new QLabel(tr("New Cards a Day")));
    m_newPerDay = new QSpinBox;
    m_newPerDay->setRange(0, 500);
    m_newPerDay->setProperty("testId", "practiceNewPerDay");
    newRow->addWidget(m_newPerDay);
    newRow->addStretch(1);
    lay->addLayout(newRow);

    lay->addSpacing(16);
    m_start = new QPushButton(tr("Start Practice"));
    m_start->setObjectName(QStringLiteral("Primary"));
    m_start->setCursor(Qt::PointingHandCursor);
    m_start->setProperty("testId", "startPractice");
    lay->addWidget(m_start, 0, Qt::AlignLeft);
    m_startHint = wrapLabel("startHint", "Muted");
    lay->addWidget(m_startHint);
    lay->addStretch(1);

    auto saveOptions = [this] {
        QStringList dirs;
        for (QCheckBox* c : m_dir)
            if (c->isChecked()) dirs << c->property("direction").toString();
        m_settings.practiceDirections = dirs.isEmpty() ? QStringList{QStringLiteral("recognize")} : dirs;
        m_settings.practiceTyped = m_typed->isChecked();
        m_settings.practiceSentenceFront = m_sentenceFront->isChecked();
        m_settings.practicePassages = m_passages->isChecked();
        m_settings.practiceNewPerDay = m_newPerDay->value();
        emit settingsChanged();
        refreshCounts();
    };
    for (QCheckBox* c : m_dir) connect(c, &QCheckBox::toggled, this, saveOptions);
    for (QCheckBox* c : {m_passages, m_starred, m_typed, m_sentenceFront}) connect(c, &QCheckBox::toggled, this, saveOptions);
    connect(m_newPerDay, &QSpinBox::valueChanged, this, saveOptions);
    connect(m_book, &QComboBox::currentIndexChanged, this, [this] { refreshCounts(); });
    connect(m_start, &QPushButton::clicked, this, [this] { startSession(); });
    return scroll;
}

core::WordLog::Filter PracticeView::filter() const
{
    core::WordLog::Filter f;
    f.directions = m_settings.practiceDirections;
    f.bookId = m_book->currentData().toInt();
    f.passages = m_settings.practicePassages;
    f.starredOnly = m_starred->isChecked();
    return f;
}

void PracticeView::showStart()
{
    // Options from the settings (without saving them back while filling in).
    for (QCheckBox* c : m_dir) {
        const QSignalBlocker b(c);
        c->setText(directionLabel(c->property("direction").toString()));
        c->setChecked(m_settings.practiceDirections.contains(c->property("direction").toString()));
    }
    {
        const QSignalBlocker b1(m_typed), b2(m_sentenceFront), b3(m_passages), b4(m_newPerDay), b5(m_book);
        m_typed->setChecked(m_settings.practiceTyped);
        m_sentenceFront->setChecked(m_settings.practiceSentenceFront);
        m_passages->setChecked(m_settings.practicePassages);
        m_newPerDay->setValue(m_settings.practiceNewPerDay);
        const int keep = m_book->currentData().toInt();
        m_book->clear();
        m_book->addItem(tr("All Books"), 0);
        for (const auto& [id, title] : m_log->booksWithWords()) m_book->addItem(title, id);
        const int at = m_book->findData(keep);
        m_book->setCurrentIndex(at < 0 ? 0 : at);
    }
    m_extraNew = 0;
    m_pages->setCurrentIndex(StartPage);
    m_tabPractice->setChecked(true);
    m_tabList->setChecked(false);
    refreshCounts();
    m_start->setFocus();
}

void PracticeView::refreshCounts()
{
    const core::PracticeCounts c = m_log->counts(filter(), now());
    const int words = m_log->count();
    m_empty->setVisible(words == 0);
    const int newLeft = std::min(c.newAvailable, std::max(0, m_settings.practiceNewPerDay - c.newToday));
    m_counts->setText(tr("%1 due · %2 new today · %3 learned · %4 in the word log")
                          .arg(count(c.dueNow, tr("card"), tr("cards")), QLocale().toString(newLeft), QLocale().toString(c.learned),
                               count(words, tr("word"), tr("words"))));
    const bool any = c.dueNow + newLeft > 0;
    m_start->setEnabled(any);
    if (any) m_startHint->clear();
    else if (words == 0) m_startHint->setText(tr("Look up some words while reading first."));
    else if (c.newAvailable > 0) m_startHint->setText(tr("Nothing due. Today's new cards are done; raise New Cards a Day for more."));
    else m_startHint->setText(tr("Nothing due now. Come back later: cards return when they are due."));
}

// ---------------------------------------------------------------- Card page

QWidget* PracticeView::buildCard()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("Page"));
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(16, 16, 16, 16);
    auto* top = new QHBoxLayout;
    m_progress = wrapLabel("cardProgress", "Muted");
    m_direction = wrapLabel("cardDirection", "Muted");
    m_direction->setAlignment(Qt::AlignRight);
    top->addWidget(m_progress);
    top->addWidget(m_direction);
    outer->addLayout(top);
    outer->addStretch(1);

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("Pop"));
    card->setAttribute(Qt::WA_StyledBackground);
    card->setMinimumWidth(360);
    card->setMaximumWidth(620);
    row->addWidget(card, 6);
    row->addStretch(1);
    outer->addLayout(row);
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(28, 24, 28, 20);
    lay->setSpacing(8);

    m_front = wrapLabel("cardFront");
    m_front->setAlignment(Qt::AlignCenter);
    QFont ff(QStringLiteral("Literata"));
    ff.setPixelSize(28);
    m_front->setFont(ff);
    lay->addWidget(m_front);
    m_frontDetail = wrapLabel("cardFrontDetail", "Muted");
    m_frontDetail->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_frontDetail);
    m_frontSentence = wrapLabel("cardFrontSentence");
    m_frontSentence->setAlignment(Qt::AlignCenter);
    m_frontSentence->setTextFormat(Qt::RichText);
    lay->addWidget(m_frontSentence);

    auto* typeRow = new QHBoxLayout;
    m_answer = new QLineEdit;
    m_answer->setPlaceholderText(tr("Type the answer"));
    m_answer->setProperty("testId", "cardAnswer");
    m_check = new QPushButton(tr("Check"));
    m_check->setProperty("testId", "cardCheck");
    typeRow->addWidget(m_answer, 1);
    typeRow->addWidget(m_check);
    lay->addLayout(typeRow);
    m_verdict = wrapLabel("cardVerdict");
    m_verdict->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_verdict);
    m_show = new QPushButton(tr("Show Answer"));
    m_show->setObjectName(QStringLiteral("Primary"));
    m_show->setCursor(Qt::PointingHandCursor);
    m_show->setToolTip(tr("Space"));
    m_show->setProperty("testId", "cardShow");
    lay->addWidget(m_show, 0, Qt::AlignCenter);

    m_backBox = new QWidget;
    auto* back = new QVBoxLayout(m_backBox);
    back->setContentsMargins(0, 8, 0, 0);
    back->setSpacing(6);
    auto* rule = new QFrame;
    rule->setFrameShape(QFrame::HLine);
    back->addWidget(rule);
    m_backMain = wrapLabel("cardBack");
    m_backMain->setAlignment(Qt::AlignCenter);
    QFont bf(QStringLiteral("Literata"));
    bf.setPixelSize(22);
    m_backMain->setFont(bf);
    back->addWidget(m_backMain);
    m_backDetail = wrapLabel("cardBackDetail");
    m_backDetail->setAlignment(Qt::AlignCenter);
    back->addWidget(m_backDetail);
    m_backSentence = wrapLabel("cardBackSentence");
    m_backSentence->setTextFormat(Qt::RichText);
    back->addWidget(m_backSentence);
    m_backSource = wrapLabel("cardSource", "Muted");
    m_backSource->setStyleSheet(QStringLiteral("font-size: 12px;"));
    back->addWidget(m_backSource);
    lay->addWidget(m_backBox);

    m_rateRow = new QWidget;
    auto* rates = new QHBoxLayout(m_rateRow);
    rates->setContentsMargins(0, 8, 0, 0);
    const QString names[4] = {tr("Again"), tr("Hard"), tr("Good"), tr("Easy")};
    const char* ids[4] = {"rateAgain", "rateHard", "rateGood", "rateEasy"};
    for (int i = 0; i < 4; ++i) {
        auto* box = new QVBoxLayout;
        box->setSpacing(2);
        m_rate[i] = new QPushButton(names[i]);
        m_rate[i]->setCursor(Qt::PointingHandCursor);
        m_rate[i]->setToolTip(tr("Key %1").arg(i + 1));
        m_rate[i]->setProperty("testId", ids[i]);
        m_rateWhen[i] = wrapLabel((QByteArray(ids[i]) + "When").constData(), "Muted");
        m_rateWhen[i]->setAlignment(Qt::AlignCenter);
        m_rateWhen[i]->setStyleSheet(QStringLiteral("font-size: 11px;"));
        box->addWidget(m_rate[i]);
        box->addWidget(m_rateWhen[i]);
        rates->addLayout(box);
        connect(m_rate[i], &QPushButton::clicked, this, [this, i] { rate(Rating(i + 1)); });
    }
    lay->addWidget(m_rateRow);

    outer->addStretch(2);
    auto* bottom = new QHBoxLayout;
    auto* end = new QPushButton(tr("End Session"));
    end->setObjectName(QStringLiteral("Link"));
    end->setCursor(Qt::PointingHandCursor);
    end->setProperty("testId", "endSession");
    bottom->addWidget(end);
    bottom->addStretch(1);
    auto* keys = wrapLabel("cardKeys", "Muted");
    keys->setText(tr("Space shows the answer · 1 to 4 rate it · Esc ends"));
    bottom->addWidget(keys);
    outer->addLayout(bottom);

    connect(m_show, &QPushButton::clicked, this, &PracticeView::revealAnswer);
    connect(m_check, &QPushButton::clicked, this, &PracticeView::checkTyped);
    // Enter in the answer box: the first checks, the next takes the suggested rating. The box keeps the key, so
    // one press never does both.
    m_answer->installEventFilter(this);
    connect(end, &QPushButton::clicked, this, &PracticeView::endSession);
    return page;
}

bool PracticeView::startSession()
{
    m_queue = m_log->session(filter(), m_settings.practiceNewPerDay + m_extraNew, now());
    if (m_queue.isEmpty()) {
        refreshCounts();
        return false;
    }
    m_done = 0;
    std::fill(std::begin(m_ratings), std::end(m_ratings), 0);
    m_tabPractice->setChecked(true);
    m_tabList->setChecked(false);
    m_pages->setCurrentIndex(CardPage);
    showCard();
    return true;
}

void PracticeView::showCard()
{
    if (m_queue.isEmpty()) {
        endSession();
        return;
    }
    m_card = m_queue.takeFirst();
    m_word = m_log->word(m_card.wordId);
    if (m_word.id == 0) {   // deleted meanwhile
        showCard();
        return;
    }
    m_revealed = false;
    m_verdictText.clear();
    m_suggested = Rating::Good;
    const QString d = m_card.direction;
    const bool sentenceFront = m_settings.practiceSentenceFront && !m_word.sentence.isEmpty() && !m_word.passage;
    QString detail;
    m_frontSentence->clear();
    if (d == QLatin1String("recall")) {
        m_front->setText(m_word.translation);
        detail = m_word.partOfSpeech;
        if (sentenceFront && !m_word.sentenceTranslation.isEmpty()) m_frontSentence->setText(m_word.sentenceTranslation.toHtmlEscaped());
    } else if (d == QLatin1String("definition")) {
        m_front->setText(m_word.definition);
        detail = m_word.partOfSpeech;
    } else if (d == QLatin1String("cloze")) {
        m_front->setText(withGap(m_word.sentence, m_word.text));
        detail = tr("Meaning: %1").arg(m_word.translation);
    } else {
        m_front->setText(m_word.text);
        QStringList parts;
        if (!m_word.lemma.isEmpty() && m_word.lemma.compare(m_word.text, Qt::CaseInsensitive) != 0) parts << tr("from %1").arg(m_word.lemma);
        if (!m_word.partOfSpeech.isEmpty()) parts << m_word.partOfSpeech;
        detail = parts.join(QStringLiteral(" · "));
        if (sentenceFront) m_frontSentence->setText(withBold(m_word.sentence, m_word.text));
    }
    QFont ff = m_front->font();
    ff.setPixelSize(d == QLatin1String("cloze") || m_front->text().size() > 60 ? 18 : 28);
    m_front->setFont(ff);
    m_frontDetail->setText(detail);
    m_frontDetail->setVisible(!detail.isEmpty());
    m_frontSentence->setVisible(!m_frontSentence->text().isEmpty());

    // Typing a long passage's translation isn't practice: those cards are always flipped.
    const bool typed = m_settings.practiceTyped && !m_word.passage;
    m_answer->clear();
    m_answer->setVisible(typed);
    m_check->setVisible(typed);
    m_show->setVisible(!typed);
    m_verdict->clear();
    m_verdict->hide();
    m_backBox->hide();
    m_rateRow->hide();
    const int total = m_done + int(m_queue.size()) + 1;
    m_progress->setText(tr("%1 of %2").arg(QLocale().toString(m_done + 1), QLocale().toString(total)));
    m_direction->setText(directionLabel(d));
    if (typed) m_answer->setFocus();
    else setFocus();
}

QStringList PracticeView::acceptedAnswers() const
{
    if (m_card.direction == QLatin1String("recognize")) return QStringList{m_word.translation} + m_word.alternatives;
    if (m_card.direction == QLatin1String("cloze")) return {m_word.text, m_word.lemma};
    return {m_word.headword(), m_word.text};
}

void PracticeView::checkTyped()
{
    if (m_revealed || page() != CardPage) return;
    using core::practice::AnswerMatch;
    const QString lang = m_card.direction == QLatin1String("recognize") ? QStringLiteral("en") : m_word.language;
    const core::practice::AnswerCheck c = core::practice::checkAnswer(m_answer->text(), acceptedAnswers(), lang);
    switch (c.match) {
    case AnswerMatch::Correct:
        m_verdictText = tr("Correct.");
        m_suggested = Rating::Good;
        break;
    case AnswerMatch::Accents:
        m_verdictText = tr("Right, but mind the accents: %1").arg(c.expected);
        m_suggested = Rating::Hard;
        break;
    case AnswerMatch::Typo:
        m_verdictText = tr("Almost: %1").arg(c.expected);
        m_suggested = Rating::Hard;
        break;
    case AnswerMatch::Wrong:
        m_verdictText = m_answer->text().trimmed().isEmpty() ? tr("The answer: %1").arg(c.expected) : tr("Not quite. The answer: %1").arg(c.expected);
        m_suggested = Rating::Again;
        break;
    }
    m_verdict->setText(m_verdictText);
    m_verdict->show();
    revealAnswer();
}

void PracticeView::revealAnswer()
{
    if (m_revealed || page() != CardPage) return;
    m_revealed = true;
    const QString d = m_card.direction;
    QStringList detail;
    if (d == QLatin1String("recognize")) {
        m_backMain->setText(m_word.translation);
        if (!m_word.alternatives.isEmpty()) detail << tr("Also: %1").arg(m_word.alternatives.join(QStringLiteral(", ")));
        if (!m_word.definition.isEmpty()) detail << m_word.definition;
    } else {
        m_backMain->setText(d == QLatin1String("cloze") ? m_word.text : m_word.headword());
        if (!m_word.lemma.isEmpty() && m_word.lemma.compare(m_word.text, Qt::CaseInsensitive) != 0)
            detail << (d == QLatin1String("cloze") ? tr("from %1").arg(m_word.lemma) : tr("As read: %1").arg(m_word.text));
        if (!m_word.partOfSpeech.isEmpty()) detail << m_word.partOfSpeech;
        if (d != QLatin1String("recall")) detail << m_word.translation;
        else if (!m_word.definition.isEmpty()) detail << m_word.definition;
    }
    m_backDetail->setText(detail.join(QLatin1Char('\n')));
    m_backDetail->setVisible(!detail.isEmpty());
    QString sentence;
    if (!m_word.sentence.isEmpty() && !m_word.passage) {
        sentence = withBold(m_word.sentence, m_word.text);
        if (!m_word.sentenceTranslation.isEmpty())
            sentence += QStringLiteral("<br><span style='color:%1'>%2</span>").arg(m_theme.muted.name(), m_word.sentenceTranslation.toHtmlEscaped());
    }
    m_backSentence->setText(sentence);
    m_backSentence->setVisible(!sentence.isEmpty());
    QString source = m_word.bookTitle;
    if (!m_word.pageLabel.isEmpty()) source += (source.isEmpty() ? QString() : QStringLiteral(" · ")) + tr("p. %1").arg(m_word.pageLabel);
    m_backSource->setText(source);
    m_backSource->setVisible(!source.isEmpty());
    m_backBox->show();
    m_show->hide();
    m_check->hide();
    m_answer->setReadOnly(true);
    updateRateButtons();
    m_rateRow->show();
    if (!m_answer->isVisible()) setFocus();
}

void PracticeView::updateRateButtons()
{
    const QDateTime t = now();
    for (int i = 0; i < 4; ++i) {
        const QDateTime due = m_scheduler.preview(m_card.memory, Rating(i + 1), t);
        m_rateWhen[i]->setText(core::practice::Scheduler::describeInterval(t, due));
        const bool suggested = Rating(i + 1) == m_suggested;
        m_rate[i]->setObjectName(suggested ? QStringLiteral("Primary") : QString());
        m_rate[i]->style()->polish(m_rate[i]);
    }
}

void PracticeView::rate(Rating r)
{
    if (!m_revealed || page() != CardPage || m_card.id == 0) return;
    const QDateTime t = now();
    core::PracticeCard after = m_card;
    after.memory = m_scheduler.review(m_card.memory, r, t);
    m_log->saveReview(m_card, after, r, t);
    ++m_done;
    ++m_ratings[int(r)];
    // A card forgotten again and again (8 times) is set aside: it needs a better meaning or a note, not more drill.
    const bool leech = r == Rating::Again && after.memory.lapses >= 8;
    if (leech) m_log->setSuspended(after.id, true);
    // Back within the next 20 minutes (Again, learning steps): later in this session.
    if (!leech && after.memory.due <= t.addSecs(20 * 60)) {
        int at = int(m_queue.size());
        // Not straight away: at least a few cards later when there are some.
        if (m_queue.size() > 3) at = std::min(int(m_queue.size()), 3 + int(m_queue.size()) / 3);
        m_queue.insert(at, after);
    }
    m_answer->setReadOnly(false);
    m_card = {};
    showCard();
}

void PracticeView::endSession()
{
    if (page() != CardPage) return;
    m_queue.clear();
    m_card = {};
    const core::PracticeCounts c = m_log->counts(filter(), now());
    QString text = m_done == 0 ? tr("No cards answered.") : tr("Done for now: %1.").arg(count(m_done, tr("card"), tr("cards")));
    if (m_done > 0)
        text += QLatin1Char('\n') + tr("Again %1 · Hard %2 · Good %3 · Easy %4")
                                        .arg(m_ratings[1]).arg(m_ratings[2]).arg(m_ratings[3]).arg(m_ratings[4]);
    if (c.dueNow > 0) text += QLatin1Char('\n') + tr("%1 still due.").arg(count(c.dueNow, tr("card is"), tr("cards are")));
    m_doneText->setText(text);
    m_moreNew->setVisible(c.newAvailable > 0);
    m_pages->setCurrentIndex(DonePage);
    m_answer->setReadOnly(false);
}

QString PracticeView::frontText() const
{
    QStringList parts{m_front->text()};
    if (!m_frontDetail->isHidden()) parts << m_frontDetail->text();
    if (!m_frontSentence->isHidden()) parts << m_frontSentence->text();
    return parts.join(QLatin1Char('\n'));
}

QString PracticeView::backText() const
{
    if (m_backBox->isHidden()) return {};
    return m_backMain->text() + QLatin1Char('\n') + m_backDetail->text();
}

QString PracticeView::lastCheck() const { return m_verdictText; }

// ---------------------------------------------------------------- Done page

QWidget* PracticeView::buildDone()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("Page"));
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(32, 48, 32, 32);
    lay->addStretch(1);
    m_doneText = wrapLabel("doneText");
    m_doneText->setAlignment(Qt::AlignCenter);
    QFont f = m_doneText->font();
    f.setPixelSize(18);
    m_doneText->setFont(f);
    lay->addWidget(m_doneText);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    m_moreNew = new QPushButton(tr("Learn 10 More New Words"));
    m_moreNew->setProperty("testId", "moreNew");
    auto* again = new QPushButton(tr("Back to Practice"));
    again->setObjectName(QStringLiteral("Primary"));
    again->setProperty("testId", "doneBack");
    row->addWidget(m_moreNew);
    row->addWidget(again);
    row->addStretch(1);
    lay->addLayout(row);
    lay->addStretch(2);
    connect(again, &QPushButton::clicked, this, &PracticeView::showStart);
    connect(m_moreNew, &QPushButton::clicked, this, [this] {
        m_extraNew += 10;
        if (!startSession()) showStart();
    });
    return page;
}

// ---------------------------------------------------------------- Word list

QWidget* PracticeView::buildList()
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("Page"));
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(8);
    auto* top = new QHBoxLayout;
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search words and meanings"));
    m_search->setClearButtonEnabled(true);
    m_search->setProperty("testId", "wordSearch");
    m_listBook = new QComboBox;
    m_listBook->setProperty("testId", "wordListBook");
    top->addWidget(m_search, 2);
    top->addWidget(m_listBook, 1);
    lay->addLayout(top);
    m_list = new QTreeWidget;
    m_list->setProperty("testId", "wordList");
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setHeaderLabels({tr("Word"), tr("Meaning"), tr("Book"), tr("Page"), tr("Looked Up"), tr("Next Review")});
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->header()->setStretchLastSection(false);
    m_list->setColumnWidth(0, 200);
    m_list->setColumnWidth(2, 200);
    m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    lay->addWidget(m_list, 1);
    m_listCount = wrapLabel("wordListCount", "Muted");
    lay->addWidget(m_listCount);
    auto* buttons = new QHBoxLayout;
    auto button = [&](const QString& text, const char* id, const QString& tip) {
        auto* b = new QPushButton(text);
        b->setProperty("testId", id);
        b->setToolTip(tip);
        buttons->addWidget(b);
        return b;
    };
    QPushButton* star = button(tr("Star"), "wordStar", tr("Starred words can be practised on their own."));
    QPushButton* show = button(tr("Show in Book"), "wordShow", tr("Opens the book where the word was last looked up."));
    QPushButton* reset = button(tr("Start Over"), "wordReset", tr("The word's cards become new again."));
    QPushButton* del = button(tr("Delete"), "wordDelete", tr("Removes the word and its cards from the word log."));
    buttons->addStretch(1);
    QPushButton* exp = button(tr("Export…"), "wordExport", tr("A tab-separated file Anki can import (File > Import), also opens in a spreadsheet."));
    lay->addLayout(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &PracticeView::fillList);
    connect(m_listBook, &QComboBox::currentIndexChanged, this, &PracticeView::fillList);
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int column) {
        if (column != 1) return;
        core::WordEntry w = m_log->word(it->data(0, Qt::UserRole).toInt());
        const QString meaning = it->text(1).trimmed();
        if (w.id == 0 || meaning.isEmpty() || meaning == w.translation) return;
        w.translation = meaning;
        m_log->update(w);   // kept over later lookups
    });
    connect(m_list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int column) {
        if (column == 1) return;   // editing the meaning
        const core::WordEntry w = m_log->word(it->data(0, Qt::UserRole).toInt());
        if (w.bookId) emit showInBookRequested(w.bookId, w.cfi, w.spine, w.start, w.end);
    });
    connect(star, &QPushButton::clicked, this, [this] {
        for (QTreeWidgetItem* it : m_list->selectedItems()) {
            core::WordEntry w = m_log->word(it->data(0, Qt::UserRole).toInt());
            w.starred = !w.starred;
            m_log->update(w);
        }
        fillList();
    });
    connect(show, &QPushButton::clicked, this, [this] {
        if (QTreeWidgetItem* it = currentListItem()) {
            const core::WordEntry w = m_log->word(it->data(0, Qt::UserRole).toInt());
            if (w.bookId) emit showInBookRequested(w.bookId, w.cfi, w.spine, w.start, w.end);
        }
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        for (QTreeWidgetItem* it : m_list->selectedItems()) m_log->resetCards(it->data(0, Qt::UserRole).toInt());
        fillList();
    });
    connect(del, &QPushButton::clicked, this, [this] {
        const QList<QTreeWidgetItem*> sel = m_list->selectedItems();
        if (sel.isEmpty()) return;
        if (m_interactive && QMessageBox::question(this, tr("Delete Words"),
                                                   tr("Delete %1 from the word log? Their practice history goes too.")
                                                       .arg(count(int(sel.size()), tr("word"), tr("words")))) != QMessageBox::Yes)
            return;
        for (QTreeWidgetItem* it : sel) m_log->remove(it->data(0, Qt::UserRole).toInt());
        fillList();
    });
    connect(exp, &QPushButton::clicked, this, [this] {
        if (!m_interactive) return;
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString path = QFileDialog::getSaveFileName(this, tr("Export Word Log"), dir + QStringLiteral("/FreeReader words.txt"),
                                                          tr("Tab-separated text (*.txt *.tsv)"));
        if (path.isEmpty()) return;
        QString err;
        if (!exportTo(path, &err)) QMessageBox::warning(this, tr("Export Word Log"), tr("Couldn't save the file. %1").arg(err));
    });
    return page;
}

QTreeWidgetItem* PracticeView::currentListItem() const
{
    if (QTreeWidgetItem* it = m_list->currentItem()) return it;
    const auto sel = m_list->selectedItems();
    return sel.isEmpty() ? nullptr : sel.first();
}

void PracticeView::showList()
{
    {
        const QSignalBlocker b(m_listBook);
        const int keep = m_listBook->currentData().toInt();
        m_listBook->clear();
        m_listBook->addItem(tr("All Books"), 0);
        for (const auto& [id, title] : m_log->booksWithWords()) m_listBook->addItem(title, id);
        const int at = m_listBook->findData(keep);
        m_listBook->setCurrentIndex(at < 0 ? 0 : at);
    }
    m_pages->setCurrentIndex(ListPage);
    m_tabList->setChecked(true);
    m_tabPractice->setChecked(false);
    fillList();
}

void PracticeView::fillList()
{
    const QSignalBlocker b(m_list);
    // The same words stay selected (after Star or Start Over).
    QSet<int> selected;
    for (QTreeWidgetItem* it : m_list->selectedItems()) selected.insert(it->data(0, Qt::UserRole).toInt());
    const int current = m_list->currentItem() ? m_list->currentItem()->data(0, Qt::UserRole).toInt() : 0;
    m_list->clear();
    const QString needle = core::practice::foldAccents(m_search->text().trimmed().toLower());
    const QDateTime t = now();
    int shown = 0;
    const QList<core::WordEntry> all = m_log->words(m_listBook->currentData().toInt());
    for (const core::WordEntry& w : all) {
        if (!needle.isEmpty()) {
            const QString hay = core::practice::foldAccents((w.text + QLatin1Char(' ') + w.lemma + QLatin1Char(' ') + w.translation + QLatin1Char(' ') +
                                                             w.alternatives.join(QLatin1Char(' '))).toLower());
            if (!hay.contains(needle)) continue;
        }
        QString next = tr("New");
        QDateTime soonest;
        for (const core::PracticeCard& c : m_log->cardsFor(w.id))
            if (!c.suspended && c.memory.state != core::practice::CardState::New && (!soonest.isValid() || c.memory.due < soonest)) soonest = c.memory.due;
        if (soonest.isValid()) next = soonest <= t ? tr("Due") : tr("In %1").arg(core::practice::Scheduler::describeInterval(t, soonest));
        QString word = (w.starred ? QStringLiteral("★ ") : QString()) + (w.passage ? w.text.left(60) + QStringLiteral("…") : w.headword());
        auto* it = new QTreeWidgetItem(m_list, {word, w.translation, w.bookTitle, w.pageLabel, QLocale().toString(w.lookups), next});
        it->setData(0, Qt::UserRole, w.id);
        it->setFlags(it->flags() | Qt::ItemIsEditable);
        it->setToolTip(0, w.text == w.headword() ? w.sentence : tr("As read: %1\n%2").arg(w.text, w.sentence));
        it->setToolTip(1, tr("Double-click to change the meaning."));
        if (selected.contains(w.id)) it->setSelected(true);
        if (w.id == current) m_list->setCurrentItem(it, 0, QItemSelectionModel::NoUpdate);
        ++shown;
    }
    m_listCount->setText(shown == int(all.size()) ? count(shown, tr("word"), tr("words"))
                                                   : tr("%1 of %2").arg(QLocale().toString(shown), count(int(all.size()), tr("word"), tr("words"))));
}

bool PracticeView::exportTo(const QString& path, QString* error) { return m_log->exportTsv(path, m_listBook->currentData().toInt(), error); }

bool PracticeView::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_answer && e->type() == QEvent::KeyPress) {
        const int k = static_cast<QKeyEvent*>(e)->key();
        if (k == Qt::Key_Return || k == Qt::Key_Enter) {
            if (!m_revealed) checkTyped();
            else rate(m_suggested);
            return true;
        }
        if (m_revealed && k >= Qt::Key_1 && k <= Qt::Key_4) {
            rate(Rating(k - Qt::Key_0));
            return true;
        }
        if (k == Qt::Key_Escape) {
            endSession();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void PracticeView::keyPressEvent(QKeyEvent* e)
{
    if (page() == CardPage) {
        const int k = e->key();
        if (k == Qt::Key_Escape) {
            endSession();
            return;
        }
        if (!m_revealed && (k == Qt::Key_Space || k == Qt::Key_Return || k == Qt::Key_Enter) && !m_answer->isVisible()) {
            revealAnswer();
            return;
        }
        if (m_revealed && k >= Qt::Key_1 && k <= Qt::Key_4) {
            rate(Rating(k - Qt::Key_0));
            return;
        }
        if (m_revealed && (k == Qt::Key_Space || k == Qt::Key_Return || k == Qt::Key_Enter)) {
            rate(m_suggested);
            return;
        }
    }
    QWidget::keyPressEvent(e);
}

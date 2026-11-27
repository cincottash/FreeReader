#include "PracticeView.h"

#include "core/practice/Session.h"

#include "ElidingComboBox.h"
#include "Icons.h"
#include "Settings.h"

#include "core/practice/Answer.h"
#include "core/translate/LanguagePacks.h"
#include "core/translate/TranslationService.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
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
#include <QWheelEvent>

#include <algorithm>

using core::practice::Rating;

namespace {

const char* kDirections[4] = {"recognize", "recall", "definition", "cloze"};

// Pages whose size is the page shown, not the largest page: the card (tall in the narrow workspace beside the book)
// made the whole workspace, and so the window, taller than a 700 px screen while the start page showed.
class CurrentPageStack : public QStackedWidget {
public:
    CurrentPageStack()
    {
        connect(this, &QStackedWidget::currentChanged, this, [this] { updateGeometry(); });
    }
    QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QSize(); }
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QStackedWidget::sizeHint(); }
};

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
    // The full window's way back to the panel beside the book (the owner's request, 2026-11-25: the panel's button
    // makes it bigger, and nothing made it smaller again).
    m_beside = new QToolButton;
    m_beside->setObjectName(QStringLiteral("Icon"));
    m_beside->setFixedSize(40, 40);
    m_beside->setIconSize(QSize(20, 20));
    m_beside->setToolTip(tr("Show Beside the Book"));
    m_beside->setCursor(Qt::PointingHandCursor);
    m_beside->setFocusPolicy(Qt::NoFocus);
    m_beside->setProperty("testId", "practiceBeside");
    m_beside->hide();
    h->addWidget(m_beside);
    connect(m_beside, &QToolButton::clicked, this, [this] {
        if (page() == CardPage) endSession();
        emit besideBookRequested();
    });
    lay->addWidget(bar);

    m_pages = new CurrentPageStack;
    m_pages->addWidget(buildStart());
    // The card scrolls when the window is short (beside the book, 700 px tall, it didn't fit). The scroll area never
    // takes the focus, so Space and 1 to 4 still reach the card.
    {
        auto* cardScroll = new QScrollArea;
        cardScroll->setWidgetResizable(true);
        cardScroll->setFrameShape(QFrame::NoFrame);
        cardScroll->setFocusPolicy(Qt::NoFocus);
        cardScroll->viewport()->setAutoFillBackground(false);
        cardScroll->setWidget(buildCard());
        cardScroll->widget()->setAutoFillBackground(false);
        m_pages->addWidget(cardScroll);
    }
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
    m_beside->setIcon(icons::icon("collapse", t.fg));   // as the panel's Restore
    m_prevPage->setIcon(icons::icon("chevronLeft", t.fg));
    m_nextPage->setIcon(icons::icon("chevronRight", t.fg));
}

void PracticeView::setCanShowBesideBook(bool on)
{
    m_canShowBeside = on;
    m_beside->setVisible(on && !m_compact);
}

PracticeView::Page PracticeView::page() const { return Page(m_pages->currentIndex()); }

QString PracticeView::languageName() const
{
    QHash<QString, int> n;
    for (const core::WordEntry& w : m_log->words()) ++n[w.language];
    // The commonest language read, other than the reader's own (its words have definitions, not translations).
    QString best;
    int most = 0;
    for (const bool skipOwn : {true, false}) {
        for (auto it = n.cbegin(); it != n.cend(); ++it)
            if (it.value() > most && it.key() != QLatin1String("und") && !(skipOwn && it.key() == core::languages::readerLanguage())) {
                most = it.value();
                best = it.key();
            }
        if (!best.isEmpty()) break;
    }
    if (best.isEmpty()) return tr("Book Language");
    return core::languages::displayTitle(best);
}

QString PracticeView::directionLabel(const QString& d) const
{
    // "Spanish → English"; in Spanish "Español → inglés" (a capital only to start the label).
    const QString lang = languageName();
    const QString mine = core::languages::readerLanguage();
    if (d == QLatin1String("recognize")) return tr("%1 → %2").arg(lang, core::languages::displayName(mine));
    if (d == QLatin1String("recall")) {
        QString bookLang = lang;
        if (!bookLang.isEmpty() && core::languages::displayName(mine) != core::languages::displayTitle(mine)) bookLang[0] = bookLang[0].toLower();
        return tr("%1 → %2").arg(core::languages::displayTitle(mine), bookLang);
    }
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
    m_startCol = col;
    m_startOuter = outer;
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
    m_startTitle = title;
    lay->addWidget(title);
    m_counts = wrapLabel("practiceCounts", "Muted");
    lay->addWidget(m_counts);
    m_empty = wrapLabel("practiceEmpty");
    // The empty list's text in title case (the owner's rule), then how to fill it: highlights only, unless Add
    // Looked-Up Words is on (2026-11-26).
    m_empty->setText(tr("No Words Yet") + QLatin1Char('\n') +
                     tr("While reading, highlight a word (right-click > Highlight): it is saved here, ready to practice."));
    lay->addWidget(m_empty);

    lay->addWidget(sectionTitle(tr("WHAT TO PRACTICE")));
    auto* bookRow = new QHBoxLayout;
    bookRow->addWidget(new QLabel(tr("Words From")));
    m_book = new ElidingComboBox;
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
    m_highlights = new QCheckBox(tr("Add Highlighted Words to the Word Log"));
    m_highlights->setToolTip(tr("Words and short phrases (up to 4 words) you highlight are saved here without looking them up. "
                                "The offline dictionary fills in the meaning when it knows the word."));
    m_highlights->setProperty("testId", "practiceHighlights");
    lay->addWidget(m_highlights);
    // Off at first: the Word List is what was highlighted, not everything looked up (the owner's choice, 2026-11-26).
    m_lookups = new QCheckBox(tr("Add Looked-Up Words to the Word Log"));
    m_lookups->setToolTip(tr("Off: only highlighted words are saved for practice. On: every word or passage translated "
                             "is saved too."));
    m_lookups->setProperty("testId", "practiceLookups");
    lay->addWidget(m_lookups);

    lay->addWidget(sectionTitle(tr("HOW")));
    m_typed = new QCheckBox(tr("Type the Answer (Instead of Flipping the Card)"));
    m_typed->setProperty("testId", "practiceTyped");
    lay->addWidget(m_typed);
    m_sentenceFront = new QCheckBox(tr("Show the Sentence on the Front"));
    m_sentenceFront->setProperty("testId", "practiceSentenceFront");
    lay->addWidget(m_sentenceFront);
    // The order the cards come in; every card is practised (no daily limits).
    auto* orderRow = new QHBoxLayout;
    orderRow->addWidget(new QLabel(tr("Order")));
    m_order = new QComboBox;
    m_order->setProperty("testId", "practiceOrder");
    m_order->addItem(tr("Random"), QStringLiteral("random"));
    m_order->addItem(tr("Alphabetical"), QStringLiteral("alphabetical"));
    m_order->addItem(tr("Newest First"), QStringLiteral("newest"));
    m_order->addItem(tr("Oldest First"), QStringLiteral("oldest"));
    m_order->addItem(tr("Book Order"), QStringLiteral("book"));
    m_order->setToolTip(tr("Book Order: each book's words in the order they come in the book."));
    orderRow->addWidget(m_order);
    orderRow->addStretch(1);
    lay->addLayout(orderRow);

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
        m_settings.practiceOrder = m_order->currentData().toString();
        m_settings.practiceHighlights = m_highlights->isChecked();
        m_settings.practiceLookups = m_lookups->isChecked();
        emit settingsChanged();
        refreshCounts();
    };
    for (QCheckBox* c : m_dir) connect(c, &QCheckBox::toggled, this, saveOptions);
    for (QCheckBox* c : {m_passages, m_starred, m_typed, m_sentenceFront, m_highlights, m_lookups}) connect(c, &QCheckBox::toggled, this, saveOptions);
    connect(m_order, &QComboBox::currentIndexChanged, this, saveOptions);
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
        const QSignalBlocker b1(m_typed), b2(m_sentenceFront), b3(m_passages), b4(m_order), b5(m_book), b6(m_highlights), b7(m_lookups);
        m_highlights->setChecked(m_settings.practiceHighlights);
        m_lookups->setChecked(m_settings.practiceLookups);
        m_typed->setChecked(m_settings.practiceTyped);
        m_sentenceFront->setChecked(m_settings.practiceSentenceFront);
        m_passages->setChecked(m_settings.practicePassages);
        m_order->setCurrentIndex(qMax(0, m_order->findData(m_settings.practiceOrder)));
        fillBookChoices(m_book, false);
    }
    m_pages->setCurrentIndex(StartPage);
    m_tabPractice->setChecked(true);
    m_tabList->setChecked(false);
    refreshCounts();
    if (!m_compact) m_start->setFocus();   // beside the book, the page keeps the keyboard
}

void PracticeView::refreshCounts()
{
    // Every card is practised, in the order chosen: no "due" or "new today".
    const int cards = m_log->practisableCount(filter());
    const int words = m_log->count();
    m_empty->setVisible(words == 0);
    m_counts->setText(tr("%1 to practice · %2 in the word log").arg(count(cards, tr("card"), tr("cards")), count(words, tr("word"), tr("words"))));
    m_start->setEnabled(cards > 0);
    if (cards > 0) m_startHint->clear();
    else if (words == 0) m_startHint->setText(tr("Translate or highlight some words while reading first."));
    else m_startHint->setText(tr("No words match these choices."));
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
    m_cardFrame = card;
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
    m_answer->setPlaceholderText(tr("Type the Answer"));
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
    rule->setObjectName(QStringLiteral("Rule"));
    rule->setFixedHeight(1);
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
    m_lookUp = new QPushButton(tr("Look Up Meaning"));
    m_lookUp->setProperty("testId", "cardLookUp");
    m_lookUp->setCursor(Qt::PointingHandCursor);
    m_lookUp->setToolTip(tr("Translates the word with the translation engines that are on, and saves the meaning."));
    back->addWidget(m_lookUp, 0, Qt::AlignCenter);
    connect(m_lookUp, &QPushButton::clicked, this, &PracticeView::lookUpMeaning);
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
    keys->setWordWrap(false);
    keys->setText(tr("Space shows the answer · 1 to 4 rate it · Esc ends"));
    m_cardKeys = keys;
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
    m_queue = m_log->allCards(filter(), m_settings.practiceOrder, now());
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
    m_lookupId = 0;   // an answer for the previous card is ignored
    m_lookUp->setText(tr("Look Up Meaning"));
    m_lookUp->setEnabled(true);
    m_lookUp->hide();
    const QString d = m_card.direction;
    const core::practice::CardFace face = core::practice::cardFace(m_word, d, m_settings.practiceSentenceFront, m_settings.practiceTyped, m_theme.muted);
    m_frontSentence->setText(face.frontSentenceHtml);
    m_front->setText(face.front);
    const QString detail = face.frontDetail;
    QFont ff = m_front->font();
    ff.setPixelSize(face.frontIsLong ? 18 : 28);
    m_front->setFont(ff);
    m_frontDetail->setText(detail);
    m_frontDetail->setVisible(!detail.isEmpty());
    m_frontSentence->setVisible(!m_frontSentence->text().isEmpty());

    const bool typed = face.typed;
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

QStringList PracticeView::acceptedAnswers() const { return core::practice::acceptedAnswers(m_word, m_card.direction); }

void PracticeView::checkTyped()
{
    if (m_revealed || page() != CardPage) return;
    using core::practice::AnswerMatch;
    const QString lang = core::practice::answerLanguage(m_word, m_card.direction);
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

void PracticeView::setTranslator(core::TranslationService* service)
{
    m_translator = service;
    if (!service) return;
    connect(service, &core::TranslationService::finished, this, [this](int id, const core::TranslationResult& r, const QString& why) {
        if (id != m_lookupId || m_lookupId == 0) return;
        m_lookupId = 0;
        m_lookUp->setText(tr("Look Up Meaning"));
        m_lookUp->setEnabled(true);
        if (page() != CardPage || m_word.id == 0) return;
        core::TranslationResult got = r;
        got.text = m_word.text;
        if (r.translation.trimmed().isEmpty() || !m_log->fillMeaning(m_word.id, got)) {
            const bool allOff = !m_settings.useGemini && !m_settings.useLmStudio && !m_settings.useDictionary && !m_settings.useOfflineTranslator;
            m_backDetail->setText(allOff ? tr("Every translation engine is turned off (Settings > Translation).")
                                         : why.isEmpty() ? tr("No answer.") : why);
            m_backDetail->show();
            return;
        }
        m_word = m_log->word(m_word.id);
        showMeaningOnCard();
    });
}

void PracticeView::lookUpMeaning()
{
    if (!m_translator || page() != CardPage || !m_revealed || m_word.id == 0 || m_lookupId) return;
    core::TranslationRequest r;
    r.text = m_word.text;
    r.sentence = m_word.sentence.size() > 600 || m_word.sentence == m_word.text ? QString() : m_word.sentence;
    r.from = m_word.language == QLatin1String("und") ? QString() : m_word.language;
    // The book's own offline choice applies here too.
    m_translator->setOfflineOnly(m_word.bookId && m_settings.offlineBooks.contains(QString::number(m_word.bookId)));
    m_lookUp->setText(tr("Looking Up…"));
    m_lookUp->setEnabled(false);
    m_lookupId = m_translator->translate(r);
}

void PracticeView::showMeaningOnCard()
{
    m_revealed = false;
    revealAnswer();
}

void PracticeView::revealAnswer()
{
    if (m_revealed || page() != CardPage) return;
    m_revealed = true;
    const core::practice::CardFace face = core::practice::cardFace(m_word, m_card.direction, m_settings.practiceSentenceFront,
                                                                   m_settings.practiceTyped, m_theme.muted);
    m_lookUp->setVisible(m_word.translation.isEmpty() && m_translator);
    m_backMain->setText(face.back);
    m_backDetail->setText(face.backDetail.join(QLatin1Char('\n')));
    m_backDetail->setVisible(!face.backDetail.isEmpty());
    m_backSentence->setText(face.backSentenceHtml);
    m_backSentence->setVisible(!face.backSentenceHtml.isEmpty());
    m_backSource->setText(face.source);
    m_backSource->setVisible(!face.source.isEmpty());
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
        m_rateWhen[i]->hide();   // no due dates any more: every card is practised each time
        Q_UNUSED(due);
        const bool suggested = Rating(i + 1) == m_suggested;
        m_rate[i]->setObjectName(suggested ? QStringLiteral("Suggested") : QString());
        m_rate[i]->style()->polish(m_rate[i]);
    }
}

void PracticeView::rate(Rating r)
{
    if (!m_revealed || page() != CardPage || m_card.id == 0) return;
    const QDateTime t = now();
    core::practice::answerCard(*m_log, m_scheduler, m_queue, m_card, r, t);
    ++m_done;
    ++m_ratings[int(r)];
    m_answer->setReadOnly(false);
    m_card = {};
    showCard();
}

void PracticeView::endSession()
{
    if (page() != CardPage) return;
    m_queue.clear();
    m_card = {};
    QString text = m_done == 0 ? tr("No cards answered.") : tr("Done for now: %1.").arg(count(m_done, tr("card"), tr("cards")));
    if (m_done > 0)
        text += QLatin1Char('\n') + tr("Again %1 · Hard %2 · Good %3 · Easy %4")
                                        .arg(m_ratings[1]).arg(m_ratings[2]).arg(m_ratings[3]).arg(m_ratings[4]);
    m_doneText->setText(text);
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
    auto* again = new QPushButton(tr("Back to Practice"));
    again->setObjectName(QStringLiteral("Primary"));
    again->setProperty("testId", "doneBack");
    row->addWidget(again);
    row->addStretch(1);
    lay->addLayout(row);
    lay->addStretch(2);
    connect(again, &QPushButton::clicked, this, &PracticeView::showStart);
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
    m_search->setPlaceholderText(tr("Search Words and Meanings"));
    m_search->setClearButtonEnabled(true);
    m_search->setProperty("testId", "wordSearch");
    m_listBook = new ElidingComboBox;
    m_listBook->setProperty("testId", "wordListBook");
    // A long title ends in "…" rather than being cut (the box is as wide as half the search box).
    m_listBook->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_listBook->setMinimumContentsLength(8);
    top->addWidget(m_search, 2);
    top->addWidget(m_listBook, 1);
    lay->addLayout(top);
    m_list = new QTreeWidget;
    m_list->setProperty("testId", "wordList");
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setHeaderLabels({tr("Word"), tr("Meaning"), tr("Book"), tr("Page"), tr("Looked Up"), tr("Next Review")});
    // Every column border is dragged to change its width (the owner's request, 2026-11-26: the meaning stretched to
    // fill the list, and a stretching column can't be dragged); the widths are kept, one set beside the book and one
    // with room (fitListColumns). The last column takes what is left.
    QHeaderView* heading = m_list->header();
    heading->setSectionResizeMode(QHeaderView::Interactive);
    heading->setStretchLastSection(true);
    heading->setMinimumSectionSize(36);
    heading->setTextElideMode(Qt::ElideRight);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(heading, &QHeaderView::sectionResized, this, [this](int column, int, int) {
        // A border dragged (the last column only follows the others, and the fitting below isn't a choice).
        if (m_fittingColumns || column == m_list->columnCount() - 1) return;
        QStringList widths;
        for (int c = 0; c < m_list->columnCount(); ++c) widths << QString::number(m_list->columnWidth(c));
        QString& kept = listIsWide() ? m_settings.wordListColumnsWide : m_settings.wordListColumns;
        if (kept == widths.join(QLatin1Char(','))) return;
        kept = widths.join(QLatin1Char(','));
        emit settingsChanged();
    });
    heading->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(heading, &QWidget::customContextMenuRequested, this, [this, heading](const QPoint& at) {
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        QAction* reset = menu->addAction(tr("Reset Column Widths"), this, &PracticeView::resetListColumns);
        reset->setProperty("testId", "wordListResetColumns");
        if (m_interactive) menu->popup(heading->mapToGlobal(at));
        else menu->deleteLater();
    });
    // Only the meaning can be changed, by double-clicking it (or right-click > Change Meaning). Double-clicking the word or the page goes
    // to the word in the book: before, any cell opened an editor, which looked broken there (2026-11-25).
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    lay->addWidget(m_list, 1);
    // Pages, as many words on each as the list has room for, instead of a scroll bar easy to miss (the owner's
    // request, 2026-11-25: "there should be multiple pages ... if it doesn't fit all in one page").
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->viewport()->installEventFilter(this);
    m_listCount = wrapLabel("wordListCount", "Muted");
    // The order of the words (the owner's request, 2026-11-26: "various ways for me to sort the words"), also by
    // clicking a column's heading. Kept in the settings.
    m_listSort = new QComboBox;
    m_listSort->setProperty("testId", "wordListSort");
    m_listSort->setToolTip(tr("The order of the words (also: click a column's heading)"));
    for (const auto& [key, label] : std::initializer_list<std::pair<const char*, QString>>{
             {"recent", tr("Recently Looked Up")}, {"newest", tr("Newest First")}, {"oldest", tr("Oldest First")},
             {"az", tr("A to Z")}, {"za", tr("Z to A")}, {"meaning", tr("Meaning A to Z")}, {"meaning-za", tr("Meaning Z to A")},
             {"book", tr("Book Order")}, {"book-reverse", tr("Book Order, Last First")}, {"lookups", tr("Most Looked Up")},
             {"lookups-least", tr("Least Looked Up")}, {"starred", tr("Starred First")}, {"review", tr("Next Review")},
             {"review-latest", tr("Next Review, Latest First")}})
        m_listSort->addItem(label, QString::fromLatin1(key));
    m_listSort->setCurrentIndex(qMax(0, m_listSort->findData(m_settings.wordListSort)));
    auto* countLine = new QHBoxLayout;
    countLine->setSpacing(8);
    countLine->addWidget(m_listCount, 1);
    auto* sortLabel = new QLabel(tr("Sort"));
    sortLabel->setObjectName(QStringLiteral("Muted"));
    countLine->addWidget(sortLabel);
    countLine->addWidget(m_listSort);
    lay->addLayout(countLine);
    connect(m_listSort, &QComboBox::currentIndexChanged, this, [this] { setListSort(m_listSort->currentData().toString()); });
    // A heading: its order; the same heading again, the other way round.
    m_list->header()->setSectionsClickable(true);
    m_list->header()->setSortIndicatorShown(true);
    connect(m_list->header(), &QHeaderView::sectionClicked, this, [this](int column) {
        // Every heading, and again the other way round (the owner's request, 2026-11-26: only Word did). Book and
        // Page both go by the place in the book.
        static const char* const pairs[][2] = {{"az", "za"},       {"meaning", "meaning-za"}, {"book", "book-reverse"},
                                               {"book", "book-reverse"}, {"lookups", "lookups-least"}, {"review", "review-latest"}};
        if (column < 0 || column >= int(std::size(pairs))) return;
        const QString first = QString::fromLatin1(pairs[column][0]), second = QString::fromLatin1(pairs[column][1]);
        setListSort(m_settings.wordListSort == first ? second : first);
    });
    // The pages' numbers under the list, as a search engine lists its result pages (the owner's request,
    // 2026-11-25): ‹ 1 2 3 … ›, the page showing in the text's color.
    auto* countRow = new QHBoxLayout;
    countRow->setSpacing(2);
    countRow->addStretch(1);
    auto pageButton = [](const char* id, const QString& tip) {
        auto* b = new QToolButton;
        b->setObjectName(QStringLiteral("Icon"));
        b->setFixedSize(30, 30);
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setProperty("testId", id);
        return b;
    };
    m_prevPage = pageButton("wordListPrevious", tr("Previous Page"));
    m_nextPage = pageButton("wordListNext", tr("Next Page"));
    m_pageNumbers = new QWidget;
    m_pageNumbers->setProperty("testId", "wordListPages");
    auto* numbers = new QHBoxLayout(m_pageNumbers);
    numbers->setContentsMargins(0, 0, 0, 0);
    numbers->setSpacing(2);
    countRow->addWidget(m_prevPage);
    countRow->addWidget(m_pageNumbers);
    countRow->addWidget(m_nextPage);
    countRow->addStretch(1);
    lay->addLayout(countRow);
    connect(m_prevPage, &QToolButton::clicked, this, [this] { showListPage(m_listPage - 1); });
    connect(m_nextPage, &QToolButton::clicked, this, [this] { showListPage(m_listPage + 1); });
    auto* buttons = new QHBoxLayout;
    auto button = [&](const QString& text, const char* id, const QString& tip) {
        auto* b = new QPushButton(text);
        b->setProperty("testId", id);
        b->setToolTip(tip);
        buttons->addWidget(b);
        return b;
    };
    QPushButton* star = button(tr("Star"), "wordStar", tr("Starred words can be practiced on their own."));
    QPushButton* show = button(tr("Show in Book"), "wordShow", tr("Opens the book where the word was last looked up."));
    QPushButton* reset = button(tr("Start Over"), "wordReset", tr("The word's cards become new again."));
    QPushButton* del = button(tr("Delete"), "wordDelete", tr("Removes the word and its cards from the word log."));
    buttons->addStretch(1);
    QPushButton* exp = button(tr("Export…"), "wordExport", tr("A tab-separated file Anki can import (File > Import), also opens in a spreadsheet."));
    m_exportButton = exp;
    lay->addLayout(buttons);

    // Another search or book: from the first page.
    connect(m_search, &QLineEdit::textChanged, this, [this] {
        m_listPage = 0;
        fillList();
    });
    connect(m_listBook, &QComboBox::currentIndexChanged, this, [this] {
        m_listPage = 0;
        fillList();
    });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int column) {
        if (column != 1) return;
        core::WordEntry w = m_log->word(it->data(0, Qt::UserRole).toInt());
        const QString meaning = it->text(1).trimmed();
        if (w.id == 0 || meaning.isEmpty() || meaning == w.translation) return;
        w.translation = meaning;
        m_log->update(w);   // kept over later lookups
    });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int column) {
        if (column == 1) m_list->editItem(it, 1);
    });
    connect(m_list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int column) {
        if (column != 1) goToWord(it);   // the meaning: being edited
    });
    // Right-click a word: go to it in the book, or change its meaning (the owner's request, 2026-11-25).
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QTreeWidgetItem* it = m_list->itemAt(at);
        if (!it) return;
        if (!it->isSelected()) m_list->setCurrentItem(it);
        std::unique_ptr<QMenu> menu(wordMenu(it));
        if (m_interactive) menu->exec(m_list->viewport()->mapToGlobal(at));
    });
    connect(star, &QPushButton::clicked, this, &PracticeView::starSelected);
    connect(show, &QPushButton::clicked, this, [this] { goToWord(currentListItem()); });
    connect(reset, &QPushButton::clicked, this, &PracticeView::startOverSelected);
    connect(del, &QPushButton::clicked, this, &PracticeView::deleteSelected);
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

void PracticeView::goToWord(QTreeWidgetItem* it)
{
    if (!it) return;
    const core::WordEntry w = m_log->word(it->data(0, Qt::UserRole).toInt());
    if (w.bookId) emit showInBookRequested(w.bookId, w.cfi, w.spine, w.start, w.end);
}

void PracticeView::starSelected()
{
    // All starred: unstar them; else star them all.
    const QList<QTreeWidgetItem*> sel = m_list->selectedItems();
    bool all = !sel.isEmpty();
    for (QTreeWidgetItem* it : sel) all = all && m_log->word(it->data(0, Qt::UserRole).toInt()).starred;
    for (QTreeWidgetItem* it : sel) {
        core::WordEntry w = m_log->word(it->data(0, Qt::UserRole).toInt());
        w.starred = !all;
        m_log->update(w);
    }
    fillList();
}

void PracticeView::startOverSelected()
{
    for (QTreeWidgetItem* it : m_list->selectedItems()) m_log->resetCards(it->data(0, Qt::UserRole).toInt());
    fillList();
}

void PracticeView::deleteSelected()
{
    const QList<QTreeWidgetItem*> sel = m_list->selectedItems();
    if (sel.isEmpty()) return;
    if (m_interactive && QMessageBox::question(this, tr("Delete Words"),
                                               tr("Delete %1 from the word log? Their practice history goes too.")
                                                   .arg(count(int(sel.size()), tr("word"), tr("words")))) != QMessageBox::Yes)
        return;
    for (QTreeWidgetItem* it : sel) m_log->remove(it->data(0, Qt::UserRole).toInt());
    fillList();
}

QMenu* PracticeView::wordMenu(QTreeWidgetItem* it)
{
    // Everything that can be done to a word, here too (the owner's request, 2026-11-25: "all of the common
    // options, even though they are already listed elsewhere"). Star, Start Over and Delete act on every word
    // selected, as their buttons do.
    auto* menu = new QMenu(this);
    if (!it->isSelected()) {
        m_list->clearSelection();
        it->setSelected(true);
    }
    m_list->setCurrentItem(it, 0, QItemSelectionModel::NoUpdate);
    const core::WordEntry w = m_log->word(it->data(0, Qt::UserRole).toInt());
    const QList<QTreeWidgetItem*> sel = m_list->selectedItems();
    const int n = int(sel.size());
    bool allStarred = n > 0;
    for (QTreeWidgetItem* s : sel) allStarred = allStarred && m_log->word(s->data(0, Qt::UserRole).toInt()).starred;
    auto add = [menu](const QString& text, const char* id) {
        QAction* a = menu->addAction(text);
        a->setProperty("testId", id);
        return a;
    };
    QAction* go = add(w.pageLabel.isEmpty() ? tr("Go to Word") : tr("Go to Word (Page %1)").arg(w.pageLabel), "wordGoTo");
    go->setEnabled(w.bookId != 0);
    if (!go->isEnabled()) go->setToolTip(tr("This word wasn't looked up in a book of the library (or its book was removed)."));
    QAction* change = add(tr("Change Meaning"), "wordChangeMeaning");
    QAction* copy = add(tr("Copy"), "wordCopy");
    menu->addSeparator();
    QAction* star = add(allStarred ? tr("Unstar") : tr("Star"), "wordMenuStar");
    QAction* again = add(tr("Start Over"), "wordMenuStartOver");
    again->setToolTip(tr("The word's cards become new again."));
    menu->addSeparator();
    QAction* del = add(n > 1 ? tr("Delete %1…").arg(count(n, tr("word"), tr("words"))) : tr("Delete…"), "wordMenuDelete");
    const QPersistentModelIndex row = m_list->indexFromItem(it);
    auto item = [this, row] { return row.isValid() ? m_list->itemFromIndex(row) : nullptr; };
    connect(go, &QAction::triggered, this, [this, item] { goToWord(item()); });
    connect(change, &QAction::triggered, this, [this, item] {
        if (QTreeWidgetItem* i = item()) m_list->editItem(i, 1);
    });
    connect(copy, &QAction::triggered, this, [this, item] {
        // The word and its meaning, one per line for each selected word.
        QStringList lines;
        for (QTreeWidgetItem* s : m_list->selectedItems()) lines << s->text(0) + QLatin1Char('\t') + s->text(1);
        if (lines.isEmpty())
            if (QTreeWidgetItem* i = item()) lines << i->text(0) + QLatin1Char('\t') + i->text(1);
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    });
    connect(star, &QAction::triggered, this, &PracticeView::starSelected);
    connect(again, &QAction::triggered, this, &PracticeView::startOverSelected);
    connect(del, &QAction::triggered, this, &PracticeView::deleteSelected);
    return menu;
}

QMenu* PracticeView::wordMenuForRow(int row)
{
    QTreeWidgetItem* it = m_list->topLevelItem(row);
    return it ? wordMenu(it) : nullptr;
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
        fillBookChoices(m_listBook, m_compact);
    }
    m_pages->setCurrentIndex(ListPage);
    m_tabList->setChecked(true);
    m_tabPractice->setChecked(false);
    fillList();
}

namespace {

// The Word column's text: a star for a starred word, a passage's start.
QString wordLabel(const core::WordEntry& w)
{
    return (w.starred ? QStringLiteral("★ ") : QString()) + (w.passage ? w.text.left(60) + QStringLiteral("…") : w.headword());
}

}  // namespace

void PracticeView::fillList()
{
    // The words matching are kept; rows are made only for the page showing. A row for every word (hidden when on
    // another page), each with a query for its next review, took over a second with the owner's 2,600 words, at
    // every opening of Practice and every step of dragging the panel's edge (2026-11-26).
    // The same words stay selected (after Star or Start Over).
    m_keepSelected.clear();
    for (QTreeWidgetItem* it : m_list->selectedItems()) m_keepSelected.insert(it->data(0, Qt::UserRole).toInt());
    m_keepCurrent = m_list->currentItem() ? m_list->currentItem()->data(0, Qt::UserRole).toInt() : 0;
    const QString needle = core::practice::foldAccents(m_search->text().trimmed().toLower());
    m_listEntries.clear();
    m_naturalWidths.clear();
    const int choice = m_listBook->currentData().toInt();
    QList<core::WordEntry> all = m_log->words(choice == kThisPage ? m_openBook : choice);
    if (choice == kThisPage) {
        // Where each word was last looked up or highlighted: on the pages showing now.
        QList<core::WordEntry> here;
        for (const core::WordEntry& w : all)
            if (m_openBook && w.spine == m_pageSpine && w.start >= m_pageStart && w.start < m_pageEnd) here << w;
        std::sort(here.begin(), here.end(), [](const core::WordEntry& a, const core::WordEntry& b) { return a.start < b.start; });
        all = here;
    }
    for (const core::WordEntry& w : all) {
        if (!needle.isEmpty()) {
            const QString hay = core::practice::foldAccents((w.text + QLatin1Char(' ') + w.lemma + QLatin1Char(' ') + w.translation + QLatin1Char(' ') +
                                                             w.alternatives.join(QLatin1Char(' '))).toLower());
            if (!hay.contains(needle)) continue;
        }
        m_listEntries << w;
    }
    sortListEntries();
    if (!listColumnsKept()) fitListColumns();   // the widths follow the words listed
    const int shown = int(m_listEntries.size());
    m_listCount->setText(shown == int(all.size()) ? count(shown, tr("word"), tr("words"))
                                                   : tr("%1 of %2").arg(QLocale().toString(shown), count(int(all.size()), tr("word"), tr("words"))));
    if (choice == kThisPage && all.isEmpty())
        m_listCount->setText(tr("No words on this page yet. Translate or highlight one and it shows here."));
    m_listDirty = true;
    showListPage(m_listPage);
}

void PracticeView::setListSort(const QString& key)
{
    if (m_listSort->findData(key) < 0) return;
    {
        const QSignalBlocker b(m_listSort);
        m_listSort->setCurrentIndex(m_listSort->findData(key));
    }
    if (m_settings.wordListSort != key) {
        m_settings.wordListSort = key;
        emit settingsChanged();
    }
    m_listPage = 0;   // another order: from its first page
    fillList();
}

QString PracticeView::listSort() const { return m_settings.wordListSort; }

void PracticeView::sortListEntries()
{
    // This Page: always the order on the page (the words are few, and read in that order).
    const bool thisPage = m_listBook->currentData().toInt() == kThisPage;
    const QString key = thisPage ? QStringLiteral("book") : m_settings.wordListSort;
    {
        // The other Practice (the full window's or the panel's) may have changed it.
        const QSignalBlocker b(m_listSort);
        m_listSort->setCurrentIndex(qMax(0, m_listSort->findData(m_settings.wordListSort)));
    }
    m_listSort->setEnabled(!thisPage);
    m_listSort->setToolTip(thisPage ? tr("This Page lists its words in the order they are on the page.")
                                    : tr("The order of the words (also: click a column's heading)"));
    // The heading's arrow on the column the order goes by (none for the orders no column shows).
    static const QHash<QString, QPair<int, Qt::SortOrder>> columns{
        {QStringLiteral("az"), {0, Qt::AscendingOrder}},          {QStringLiteral("za"), {0, Qt::DescendingOrder}},
        {QStringLiteral("meaning"), {1, Qt::AscendingOrder}},     {QStringLiteral("meaning-za"), {1, Qt::DescendingOrder}},
        {QStringLiteral("book"), {3, Qt::AscendingOrder}},        {QStringLiteral("book-reverse"), {3, Qt::DescendingOrder}},
        {QStringLiteral("lookups"), {4, Qt::DescendingOrder}},    {QStringLiteral("lookups-least"), {4, Qt::AscendingOrder}},
        {QStringLiteral("review"), {5, Qt::AscendingOrder}},      {QStringLiteral("review-latest"), {5, Qt::DescendingOrder}}};
    const auto col = columns.value(key, {-1, Qt::AscendingOrder});
    m_list->header()->setSortIndicator(col.first, col.second);
    if (key == QLatin1String("recent")) return;   // as the word log gives them
    auto folded = [](const QString& s) { return core::practice::foldAccents(s.toLower()); };
    auto alpha = [&](const QString& a, const QString& b) { return QString::localeAwareCompare(folded(a), folded(b)); };
    auto& v = m_listEntries;
    using W = core::WordEntry;
    // Each order's "comes first" rule; the second of a pair (a heading clicked again) is the same rule the other way.
    std::function<bool(const W&, const W&)> before;
    const QString base = key.section(QLatin1Char('-'), 0, 0);
    if (key == QLatin1String("newest")) before = [](const W& a, const W& b) { return a.added > b.added; };
    else if (key == QLatin1String("oldest")) before = [](const W& a, const W& b) { return a.added < b.added; };
    else if (key == QLatin1String("az") || key == QLatin1String("za")) before = [&](const W& a, const W& b) { return alpha(a.headword(), b.headword()) < 0; };
    else if (base == QLatin1String("meaning")) before = [&](const W& a, const W& b) { return alpha(a.translation, b.translation) < 0; };
    else if (base == QLatin1String("book"))
        before = [&](const W& a, const W& b) {
            if (a.bookTitle != b.bookTitle) return alpha(a.bookTitle, b.bookTitle) < 0;
            if (a.spine != b.spine) return a.spine < b.spine;
            return a.start < b.start;
        };
    else if (base == QLatin1String("lookups")) before = [](const W& a, const W& b) { return a.lookups > b.lookups; };
    else if (key == QLatin1String("starred")) before = [](const W& a, const W& b) { return a.starred && !b.starred; };
    else if (base == QLatin1String("review")) {
        // The soonest review first (latest, the other way); words not practised yet (New) after them either way.
        const QHash<int, QDateTime> due = m_log->nextReviews();
        const bool latest = key == QLatin1String("review-latest");
        std::stable_sort(v.begin(), v.end(), [&](const W& a, const W& b) {
            const QDateTime da = due.value(a.id), db = due.value(b.id);
            if (da.isValid() != db.isValid()) return da.isValid();
            return da.isValid() && (latest ? db < da : da < db);
        });
        return;
    }
    if (!before) return;
    const bool reversed = key == QLatin1String("za") || key.contains(QLatin1Char('-'));
    if (reversed) std::stable_sort(v.begin(), v.end(), [&](const W& a, const W& b) { return before(b, a); });
    else std::stable_sort(v.begin(), v.end(), before);
}

void PracticeView::buildListRows(int first, int count)
{
    const QSignalBlocker b(m_list);
    m_list->setUpdatesEnabled(false);
    m_list->clear();
    const QDateTime t = now();
    const int end = qMin(int(m_listEntries.size()), first + count);
    for (int i = first; i < end; ++i) {
        const core::WordEntry& w = m_listEntries[i];
        QString next = tr("New");
        QDateTime soonest;
        for (const core::PracticeCard& c : m_log->cardsFor(w.id))
            if (!c.suspended && c.memory.state != core::practice::CardState::New && (!soonest.isValid() || c.memory.due < soonest)) soonest = c.memory.due;
        if (soonest.isValid()) next = soonest <= t ? tr("Due") : tr("In %1").arg(core::practice::Scheduler::describeInterval(t, soonest));
        auto* it = new QTreeWidgetItem(m_list, {wordLabel(w), w.translation, w.bookTitle, w.pageLabel, QLocale().toString(w.lookups), next});
        it->setData(0, Qt::UserRole, w.id);
        it->setFlags(it->flags() | Qt::ItemIsEditable);
        it->setToolTip(0, w.text == w.headword() ? w.sentence : tr("As read: %1\n%2").arg(w.text, w.sentence));
        it->setToolTip(1, tr("Double-click to change the meaning."));
        if (m_keepSelected.contains(w.id)) it->setSelected(true);
        if (w.id == m_keepCurrent) m_list->setCurrentItem(it, 0, QItemSelectionModel::NoUpdate);
    }
    m_list->setUpdatesEnabled(true);
    m_list->scrollToTop();
    m_keepSelected.clear();
    m_keepCurrent = 0;
}

int PracticeView::listRowsPerPage() const
{
    // As many rows as the list has room for (the rows are all as tall); before a row was measured or the list
    // laid out, 25.
    if (m_list->topLevelItemCount()) m_rowHeight = m_list->sizeHintForRow(0);
    const int room = m_list->viewport()->height();
    if (m_rowHeight <= 0 || room < m_rowHeight * 3) return 25;
    return qMax(3, room / m_rowHeight);
}

void PracticeView::showListPage(int page)
{
    const int n = int(m_listEntries.size());
    int per = listRowsPerPage();
    m_listPages = qMax(1, (n + per - 1) / per);
    m_listPage = qBound(0, page, m_listPages - 1);
    if (m_listDirty || m_builtPage != m_listPage || m_builtPer != per) {
        buildListRows(m_listPage * per, per);
        // The first rows made measure a row: the page again with as many as fit.
        if (const int measured = listRowsPerPage(); measured != per) {
            const int firstWord = m_listPage * per;
            per = measured;
            m_listPages = qMax(1, (n + per - 1) / per);
            m_listPage = qBound(0, firstWord / per, m_listPages - 1);
            buildListRows(m_listPage * per, per);
        }
        m_listDirty = false;
        m_builtPage = m_listPage;
        m_builtPer = per;
    }
    updatePageNumbers();
}

void PracticeView::updatePageNumbers()
{
    const bool paged = m_listPages > 1;
    m_prevPage->setVisible(paged);
    m_nextPage->setVisible(paged);
    m_pageNumbers->setVisible(paged);
    m_prevPage->setEnabled(m_listPage > 0);
    m_nextPage->setEnabled(m_listPage < m_listPages - 1);
    // Up to 10 numbers (fewer when the list is narrow), around the page showing. Made again only when they change
    // (not at every step of dragging the panel's edge).
    const int room = (m_list->width() > 0 ? m_list->width() : 400) - 2 * 34;
    const int most = qBound(3, room / 30, 10);
    const int shown = qMin(most, m_listPages);
    const int from = qBound(0, m_listPage - shown / 2, m_listPages - shown);
    const QString made = paged ? QStringLiteral("%1 %2 %3 %4").arg(from).arg(shown).arg(m_listPage).arg(m_listPages) : QStringLiteral("-");
    if (made == m_numbersMade) return;
    m_numbersMade = made;
    for (QPushButton* b : m_pageNumbers->findChildren<QPushButton*>(Qt::FindDirectChildrenOnly)) {
        // Out of the row at once, deleted later (a click on one of them runs this).
        m_pageNumbers->layout()->removeWidget(b);
        b->hide();
        b->setParent(nullptr);
        b->deleteLater();
    }
    if (!paged) return;
    for (int p = from; p < from + shown; ++p) {
        auto* b = new QPushButton(QLocale().toString(p + 1));
        const bool here = p == m_listPage;
        b->setObjectName(here ? QString() : QStringLiteral("Link"));
        b->setFlat(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(here ? Qt::ArrowCursor : Qt::PointingHandCursor);
        b->setProperty("testId", QByteArray("wordListPage-") + QByteArray::number(p + 1));
        b->setToolTip(tr("Page %1 of %2").arg(QLocale().toString(p + 1), QLocale().toString(m_listPages)));
        b->setStyleSheet(here ? QStringLiteral("QPushButton { border: 0; background: transparent; padding: 2px 6px; font-weight: 700; }")
                              : QStringLiteral("QPushButton { border: 0; background: transparent; padding: 2px 6px; }"));
        connect(b, &QPushButton::clicked, this, [this, p] { showListPage(p); });
        m_pageNumbers->layout()->addWidget(b);
    }
}

bool PracticeView::exportTo(const QString& path, QString* error)
{
    const int choice = m_listBook->currentData().toInt();
    return m_log->exportTsv(path, choice == kThisPage ? m_openBook : choice, error);
}

void PracticeView::fillBookChoices(QComboBox* box, bool thisPage)
{
    const int keep = box->currentData().isValid() ? box->currentData().toInt() : 0;
    box->clear();
    if (thisPage && m_openBook) box->addItem(tr("This Page"), kThisPage);
    box->addItem(tr("All Books"), 0);
    bool listed = false;
    // A long title is shortened in the list (the whole title is its tooltip), so the box beside the book shows it.
    auto addBook = [box](const QString& title, int id) {
        const QString shown = title.size() > 40 ? title.left(38).trimmed() + QStringLiteral("…") : title;
        box->addItem(shown, id);
        box->setItemData(box->count() - 1, title, Qt::ToolTipRole);
    };
    for (const auto& [id, title] : m_log->booksWithWords()) {
        addBook(title, id);
        if (id == m_openBook) listed = true;
    }
    // The book being read, even before it has words.
    if (m_openBook && !listed) addBook(m_openTitle.isEmpty() ? tr("This Book") : m_openTitle, m_openBook);
    int want = keep;
    if (box->property("chooseOpen").toBool()) {
        want = m_openBook;
        box->setProperty("chooseOpen", false);
    }
    const int at = box->findData(want);
    box->setCurrentIndex(at < 0 ? qMax(0, box->findData(0)) : at);
}

void PracticeView::setCompact(bool on)
{
    m_compact = on;
    m_back->setVisible(!on);
    m_heading->setVisible(!on);
    m_beside->setVisible(!on && m_canShowBeside);
    m_tabPractice->setText(on ? tr("Flashcards") : tr("Practice"));
    // Beside the book the column fills the panel; in the whole window it is centred.
    m_startCol->setMinimumWidth(on ? 0 : 320);
    m_startOuter->setStretch(0, on ? 0 : 1);
    m_startOuter->setStretch(2, on ? 0 : 1);
    m_startOuter->setContentsMargins(on ? 4 : 11, on ? 0 : 11, on ? 4 : 11, 11);
    QFont tf = m_startTitle->font();
    tf.setPixelSize(on ? 18 : 24);
    m_startTitle->setFont(tf);
    m_startTitle->setText(on ? tr("Practice While You Read") : tr("Practice Your Words"));
    m_cardFrame->setMinimumWidth(on ? 0 : 360);
    m_cardKeys->setWordWrap(on);
    applyListColumns();
}

void PracticeView::setWide(bool on)
{
    m_wide = on;
    applyListColumns();
}

bool PracticeView::listIsWide() const { return !m_compact || m_wide; }

void PracticeView::applyListColumns()
{
    // Every column, beside the book too (the owner's request, 2026-11-26: the book, lookups and next review showed
    // only with the panel maximized); Export when there is room: the full window, or the panel maximized.
    m_exportButton->setVisible(listIsWide());
    fitListColumns();
}

void PracticeView::fitListColumns()
{
    // The widths dragged before for this room (beside the book, or with room); else fitted to the room.
    m_fittingColumns = true;
    const bool wide = listIsWide();
    const QStringList kept = (wide ? m_settings.wordListColumnsWide : m_settings.wordListColumns).split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (kept.size() == m_list->columnCount()) {
        for (int c = 0; c + 1 < m_list->columnCount(); ++c) m_list->setColumnWidth(c, qMax(36, kept[c].toInt()));
    } else {
        // Decided by the room, not by where the panel is: each column as wide as its longest entry (word, meaning and
        // book within a limit), and the room left shared by those three in proportion. The meaning taking it all
        // left a wide gap between the meanings and the book on a big screen, maximized (the owner's report,
        // 2026-11-26) and beside the book made wide by dragging its edge (2026-11-27).
        const int room = m_list->viewport()->width() > 50 ? m_list->viewport()->width() : (wide ? 900 : 440);
        const QList<int> w = wideColumnWidths(room);
        int total = 0;
        for (int v : w) total += v;
        if (wide || total <= room) {
            for (int c = 0; c + 1 < m_list->columnCount(); ++c) m_list->setColumnWidth(c, w[c]);
        } else {
            // Too narrow for that beside the book: word, (meaning), book, page, looked up, next review at small fixed
            // widths, so all six fit the panel's usual 480 px (narrower, the list scrolls sideways).
            const QList<int> n{96, 0, 88, 40, 54, 74};
            int others = 0;
            for (int c = 0; c < m_list->columnCount(); ++c)
                if (c != 1) {
                    m_list->setColumnWidth(c, n[c]);
                    others += n[c];
                }
            m_list->setColumnWidth(1, qMax(80, room - others));
        }
    }
    m_fittingColumns = false;
}

QList<int> PracticeView::wideColumnWidths(int room) const
{
    // The widths the words listed need, worked out once per list and font (the panel's edge dragged refits at every
    // step; measured before the list had its font, the columns came out too narrow).
    const QString font = m_list->font().key() + QLatin1Char('|') + m_list->header()->font().key();
    if (m_naturalWidths.size() != m_list->columnCount() || m_naturalFont != font) {
        m_naturalWidths = naturalColumnWidths();
        m_naturalFont = font;
    }
    QList<int> want = m_naturalWidths;
    // The last column stretches, but never below the header's default size: counted at that, or the columns end
    // past the right edge.
    want.last() = qMax(want.last(), m_list->header()->defaultSectionSize());
    int total = 0;
    for (int v : want) total += v;
    if (total < room) {
        const int spare = room - total, text = want[0] + want[1] + want[2];
        for (int c = 0; c < 3; ++c) want[c] += spare * want[c] / qMax(1, text);
    } else {
        // Too little room: the meaning gives way first, then the word, then the book (then the list scrolls).
        int over = total - room;
        for (int c : {1, 0, 2}) {
            const int give = qMin(over, qMax(0, want[c] - 100));
            want[c] -= give;
            over -= give;
        }
    }
    return want;
}

QList<int> PracticeView::naturalColumnWidths() const
{
    const QFontMetrics fm(m_list->font());
    const QFontMetrics hm(m_list->header()->font());
    const int pad = 28;   // the cell's margins and a little air
    QList<int> want;
    for (int c = 0; c < m_list->columnCount(); ++c) want << hm.horizontalAdvance(m_list->headerItem()->text(c)) + pad;
    auto widen = [&](int c, const QString& text) { want[c] = qMax(want[c], fm.horizontalAdvance(text) + pad); };
    for (const core::WordEntry& e : m_listEntries) {
        widen(0, wordLabel(e));
        widen(1, e.translation);
        widen(2, e.bookTitle);
        widen(3, e.pageLabel);
    }
    widen(4, QStringLiteral("9999"));
    widen(5, tr("In %1").arg(QStringLiteral("30 days")));
    const QList<int> most{260, 420, 260};   // word, meaning, book: longer ones end in "…"
    for (int c = 0; c < 3; ++c) want[c] = qMin(want[c], most[c]);
    return want;
}

bool PracticeView::listColumnsKept() const
{
    return !(listIsWide() ? m_settings.wordListColumnsWide : m_settings.wordListColumns).isEmpty();
}

void PracticeView::resetListColumns()
{
    QString& kept = listIsWide() ? m_settings.wordListColumnsWide : m_settings.wordListColumns;
    if (kept.isEmpty()) return;
    kept.clear();
    fitListColumns();
    emit settingsChanged();
}

void PracticeView::setOpenBook(int bookId, const QString& title)
{
    if (bookId == m_openBook && title == m_openTitle) return;
    m_openBook = bookId;
    m_openTitle = title;
    m_book->setProperty("chooseOpen", true);
    m_listBook->setProperty("chooseOpen", true);
}

void PracticeView::setPageRange(int spine, int start, int end)
{
    m_pageSpine = spine;
    m_pageStart = start;
    m_pageEnd = end;
}

void PracticeView::refresh()
{
    if (page() == StartPage) showStart();
    else if (page() == ListPage) {
        if (m_listBook->property("chooseOpen").toBool()) showList();
        else fillList();
    }
}

void PracticeView::setListBook(int choice)
{
    const int at = m_listBook->findData(choice);
    if (at >= 0) m_listBook->setCurrentIndex(at);
}

int PracticeView::listBook() const { return m_listBook->currentData().toInt(); }

QStringList PracticeView::listWords() const
{
    QStringList out;
    for (const core::WordEntry& w : m_listEntries) out << wordLabel(w);
    return out;
}

bool PracticeView::eventFilter(QObject* o, QEvent* e)
{
    if (m_list && o == m_list->viewport()) {
        if (e->type() == QEvent::Resize) {
            // Another height, another number of rows a page: the page that has the first word shown now. Only the
            // page numbers when the rows a page stay the same (a panel made wider or narrower).
            showListPage(m_listPage * qMax(1, m_builtPer) / listRowsPerPage());
            // Another width: the meaning fills it again, unless widths were dragged.
            if (!listColumnsKept()) fitListColumns();
        } else if (e->type() == QEvent::Wheel && m_listPages > 1) {
            // The wheel turns the pages (there is no scrolling within one).
            auto* w = static_cast<QWheelEvent*>(e);
            // Sideways (a tilted wheel, a touchpad): the columns scroll, when they don't all fit.
            if (w->angleDelta().y() == 0) return QWidget::eventFilter(o, e);
            m_wheel += w->angleDelta().y();
            if (qAbs(m_wheel) >= 120) {
                showListPage(m_listPage + (m_wheel < 0 ? 1 : -1));
                m_wheel = 0;
            }
            return true;
        }
    }
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

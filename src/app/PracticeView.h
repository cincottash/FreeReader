// Practice: flashcards made from the word log, with spaced repetition (FSRS).
// - Start page: what to practise (all books or one, which directions, passages, starred only) and how (flip the
//   card or type the answer, the sentence on the front, new words per day), with today's counts.
// - A session: one card at a time. Show Answer (Space) or type and Check (Enter), then Again / Hard / Good / Easy
//   (1-4), each with when the card comes back. Cards answered Again come back in the same session.
// - A highlighted word with no meaning yet is practised from the book's language only; its card has a Look Up
//   Meaning button (the only lookup Practice ever makes, and only when pressed).
// - Word List: every word looked up or highlighted, searchable; edit the meaning, star, start over, delete, show it in the book,
//   export for Anki.
#pragma once

#include "Theme.h"

#include "core/practice/Scheduler.h"
#include "core/services/WordLog.h"

#include <QDateTime>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
struct Settings;

namespace core {
class TranslationService;
struct TranslationResult;
}

class PracticeView : public QWidget {
    Q_OBJECT
public:
    enum Page { StartPage = 0, CardPage = 1, DonePage = 2, ListPage = 3 };

    PracticeView(core::WordLog* log, Settings& settings, QWidget* parent = nullptr);
    void setTheme(const Theme& t);
    void setInteractive(bool on) { m_interactive = on; }
    void setClock(std::function<QDateTime()> now) { m_now = std::move(now); }   // tests
    // For the Look Up Meaning button on a card whose word has no meaning yet (a highlighted word).
    void setTranslator(core::TranslationService* service);
    void lookUpMeaning();

    void showStart();           // refreshes counts
    void showList();
    Page page() const;

    // Session (also driven by tests)
    bool startSession();        // false when there is nothing to practise
    void revealAnswer();
    void checkTyped();          // type mode: compares and suggests a rating
    void rate(core::practice::Rating r);
    void endSession();
    int remaining() const { return int(m_queue.size()); }
    const core::PracticeCard& current() const { return m_card; }
    QString frontText() const;
    QString backText() const;
    QString lastCheck() const;   // the typed-answer verdict shown
    core::practice::Rating suggestedRating() const { return m_suggested; }

    bool exportTo(const QString& path, QString* error);

    // Labels of the directions for the most common language in the log ("Spanish → English").
    QString directionLabel(const QString& direction) const;

signals:
    void backRequested();
    void showInBookRequested(int bookId, const QString& cfi, int spine, int start, int end);
    void settingsChanged();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QWidget* buildStart();
    QWidget* buildCard();
    QWidget* buildDone();
    QWidget* buildList();
    core::WordLog::Filter filter() const;
    void refreshCounts();
    void fillList();
    void showCard();
    void updateRateButtons();
    QDateTime now() const { return m_now ? m_now() : QDateTime::currentDateTimeUtc(); }
    QStringList acceptedAnswers() const;
    QString languageName() const;
    QTreeWidgetItem* currentListItem() const;

    void showMeaningOnCard();

    core::WordLog* m_log;
    core::TranslationService* m_translator = nullptr;
    int m_lookupId = 0;
    Settings& m_settings;
    Theme m_theme = Theme::midnight();
    bool m_interactive = true;
    std::function<QDateTime()> m_now;
    core::practice::Scheduler m_scheduler;

    QToolButton* m_back;
    QLabel* m_heading;
    QPushButton* m_tabPractice;
    QPushButton* m_tabList;
    QStackedWidget* m_pages;

    // Start
    QLabel* m_counts;
    QLabel* m_empty;
    QComboBox* m_book;
    QCheckBox* m_dir[4];
    QCheckBox* m_passages;
    QCheckBox* m_starred;
    QCheckBox* m_highlights;
    QCheckBox* m_typed;
    QCheckBox* m_sentenceFront;
    QComboBox* m_order;
    QPushButton* m_start;
    QLabel* m_startHint;

    // Card
    QLabel* m_progress;
    QLabel* m_direction;
    QLabel* m_front;
    QLabel* m_frontDetail;
    QLabel* m_frontSentence;
    QLineEdit* m_answer;
    QPushButton* m_check;
    QPushButton* m_show;
    QLabel* m_verdict;
    QWidget* m_backBox;
    QLabel* m_backMain;
    QLabel* m_backDetail;
    QLabel* m_backSentence;
    QLabel* m_backSource;
    QPushButton* m_lookUp;
    QWidget* m_rateRow;
    QPushButton* m_rate[4];
    QLabel* m_rateWhen[4];

    // Done
    QLabel* m_doneText;

    // List
    QLineEdit* m_search;
    QComboBox* m_listBook;
    QTreeWidget* m_list;
    QLabel* m_listCount;

    // Session state
    QList<core::PracticeCard> m_queue;
    core::PracticeCard m_card;
    core::WordEntry m_word;
    bool m_revealed = false;
    QString m_verdictText;
    core::practice::Rating m_suggested = core::practice::Rating::Good;
    int m_done = 0;
    int m_ratings[5] = {0, 0, 0, 0, 0};
};

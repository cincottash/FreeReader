// Practice: flashcards made from the word log, with spaced repetition (FSRS).
// - Start page: what to practise (all books or one, which directions, passages, starred only) and how (flip the
//   card or type the answer, the sentence on the front, new words per day), with today's counts.
// - A session: one card at a time. Show Answer (Space) or type and Check (Enter), then Again / Hard / Good / Easy
//   (1-4), each with when the card comes back. Cards answered Again come back in the same session.
// - A highlighted word with no meaning yet is practised from the book's language only; its card has a Look Up
//   Meaning button (the only lookup Practice ever makes, and only when pressed).
// - Word List: every word looked up or highlighted, searchable; edit the meaning, star, start over, delete, show it in the book,
//   export for Anki.
// - Compact (the practice workspace beside the page while reading): no Back button or heading, narrower layouts,
//   the open book chosen in Words From and the Word List, and This Page in the Word List (the words looked up or
//   highlighted on the pages showing).
#pragma once

#include "Theme.h"

#include "core/practice/Scheduler.h"
#include "core/services/WordLog.h"

#include <QDateTime>
#include <QSet>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
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

    // The practice workspace while reading.
    void setCompact(bool on);
    bool isCompact() const { return m_compact; }
    // Compact but with room (the panel maximized over the page): the Word List's other columns and Export too.
    void setWide(bool on);
    // The book being read (0: none): chosen in Words From and the Word List the next time either is shown.
    void setOpenBook(int bookId, const QString& title);
    // The pages showing now (one spine item, offsets start..end), for This Page in the Word List.
    void setPageRange(int spine, int start, int end);
    // After words were added or the page turned: the start page's counts or the Word List again (never during a
    // session).
    void refresh();
    // The Word List's choice: kThisPage, 0 (All Books) or a book id.
    static constexpr int kThisPage = -1;
    void setListBook(int choice);
    int listBook() const;
    QStringList listWords() const;   // the words the Word List shows (tests)
    // The Word List's pages (as many words a page as it has room for): 0-based; out of range is clamped.
    void showListPage(int page);
    int listPage() const { return m_listPage; }
    int listPageCount() const { return m_listPages; }
    // A word's right-click menu in the Word List (Go to Word, Change Meaning); the caller owns it (tests).
    class QMenu* wordMenuForRow(int row);
    // The full window's button back to the book, with Practice beside it: shown when there is a book to go back to.
    void setCanShowBesideBook(bool on);

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
    void besideBookRequested();   // the full window's Show Beside the Book
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
    void goToWord(QTreeWidgetItem* it);
    void starSelected();
    void startOverSelected();
    void deleteSelected();
    int listRowsPerPage() const;
    void buildListRows(int first, int count);   // the rows of the page showing (only those are made)
    void updatePageNumbers();
    class QToolButton* m_prevPage = nullptr;
    class QToolButton* m_nextPage = nullptr;
    QWidget* m_pageNumbers = nullptr;   // the pages' numbers between Previous and Next
    int m_listPage = 0, m_listPages = 1;
    QList<core::WordEntry> m_listEntries;   // every word the Word List has (search and book applied), in order
    bool m_listDirty = true;                 // the rows showing need making again
    int m_builtPage = -1, m_builtPer = 0;    // the page and rows a page the rows showing were made for
    mutable int m_rowHeight = 0;             // a row's height, once measured (every row is as tall)
    QSet<int> m_keepSelected;                // word ids selected again when the rows are made again
    int m_keepCurrent = 0;
    QString m_numbersMade;                   // what the page numbers showing were made for
    int m_wheel = 0;
    class QMenu* wordMenu(QTreeWidgetItem* it);
    class QToolButton* m_beside = nullptr;
    bool m_canShowBeside = false;

    void showMeaningOnCard();
    void fillBookChoices(QComboBox* box, bool thisPage);

    bool m_compact = false;
    bool m_wide = false;
    void applyListColumns();
    int m_openBook = 0;
    QString m_openTitle;
    int m_pageSpine = -1, m_pageStart = 0, m_pageEnd = 0;
    QWidget* m_startCol = nullptr;
    class QHBoxLayout* m_startOuter = nullptr;
    QLabel* m_startTitle = nullptr;
    QFrame* m_cardFrame = nullptr;
    QLabel* m_cardKeys = nullptr;
    QPushButton* m_exportButton = nullptr;

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
    QTreeWidget* m_list = nullptr;
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

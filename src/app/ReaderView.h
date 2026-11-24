// The reading screen: header (back to the library, title and chapter, contents, text and layout, one or two pages,
// themes, the Notebook, Practice, full screen), the contents panel, the pages, a panel on the right (the Notebook,
// English alongside or the practice workspace), the progress footer, Back after a jump, and footnote pop-ups.
#pragma once

#include "PageView.h"
#include "Theme.h"

#include "core/position/Position.h"
#include "core/translate/Translation.h"

#include <QDateTime>
#include <QJsonValue>
#include <QHash>
#include <QList>
#include <QLocale>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <vector>

class AppearancePanel;
class FooterBar;
class NotePopup;
class QLabel;
class QPushButton;
class QToolButton;
class TocPanel;
class NotebookPanel;
class BilingualPanel;
class PracticePanel;
class PracticeView;
class SearchPanel;
class TranslationPopup;
class DictionaryPopup;
class QMenu;
class QTextToSpeech;
struct Settings;

namespace core {
struct KindlePlan;
class OfflineTranslator;
class OfflineDictionary;
class BookEngine;
class Library;
class TranslationService;
class WordLog;
class Annotations;
struct SearchHit;
struct TranslationResult;
struct WordPlace;
class ReadingLog;
}

class ReaderView : public QWidget {
    Q_OBJECT
public:
    ReaderView(core::BookEngine* engine, core::Library* library, Settings& settings, QWidget* parent = nullptr);
    ~ReaderView() override;

    void showBook(int bookId, const core::Position& start);
    void closeBook();            // saves the position
    void applyReadingOptions();  // the appearance in effect for this book (All Books, its language, the book)
    Appearance effectiveAppearance() const;
    // A change from the Aa panel or a command, written to the panel's current scope.
    void changeAppearance(const QString& key, const QJsonValue& value);
    AppearancePanel* appearancePanel() const { return m_panel; }
    void showAppearancePanel();
    // Text size and margins by a step (0: back to the default), with a short notice on the page.
    void stepTextSize(int delta);
    void stepMargins(int delta);
    QString languageCode() const;   // "es" for "es-ES"; empty when the book doesn't say
    QString languageName() const;   // "Spanish"
    void setSettingsNotKept(bool on);
    void setInteractive(bool on) { m_interactive = on; }   // off: never opens the browser (tests, screenshots)
    void setTheme(const Theme& t);
    void updateThemeDots();

    PageView* pages() const { return m_pages; }
    TocPanel* contents() const { return m_toc; }
    FooterBar* footer() const { return m_footer; }
    NotePopup* notePopup() const { return m_note; }
    bool contentsVisible() const;
    void setContentsVisible(bool on);
    bool isSettled() const;      // the book, its index and the current page are ready

    // Navigation
    void jumpTo(const core::Position& p);   // remembers where it came from (Back)
    bool canGoBack() const { return !m_history.empty(); }
    void goBack();
    void forgetHistory();   // no Back (the screenshot mode's own jumps aren't the reader's)
    void nextChapter();
    void previousChapter();
    QString goToPageOrPercent(const QString& text);   // empty on success, else a message for the user
    void setFullScreenIcon(bool fullScreen);

    // Translation of the selection (the pop-up), and the right-click menu on the page.
    void setTranslator(core::TranslationService* service);
    void setWordLog(core::WordLog* log) { m_words = log; }
    void setOfflineTranslator(core::OfflineTranslator* t) { m_offlineTranslator = t; }
    // The offline dictionaries the Dictionary card reads (definitions first, then translating ones).
    void setDictionaries(const QList<core::OfflineDictionary*>& d) { m_dictionaries = d; }
    int bookId() const { return m_bookId; }

    // Highlights, notes, bookmarks (phase 4).
    void setAnnotations(core::Annotations* a) { m_notes = a; }
    // Kindle highlights (see core/services/KindleImport.h) into the open book: highlights with their notes, words
    // for the Word List (also those this edition words differently, at about their place), bookmarks, and the
    // Kindle reading place. Anything already there is skipped, so importing twice adds nothing.
    struct KindleOptions {
        bool highlights = true, words = true, missingWords = true, missingPassages = true, bookmarks = true, moveToLastRead = true;
        bool equivalents = true;   // highlights this edition words differently: on their equivalent words, in orange
    };
    struct KindleResult {
        int highlights = 0, equivalents = 0, words = 0, missingWords = 0, passages = 0, bookmarks = 0, skipped = 0;
        bool moved = false;
    };
    KindleResult importKindle(const core::KindlePlan& plan, const KindleOptions& options);
    // The highlight's id; an existing one changes color. `chosen`: picked by the reader (it moves to the front of the
    // dots), not the yellow a note gets.
    int highlightSelection(const QString& color, bool chosen = true);
    bool removeHighlightAtSelection();
    int highlightAtSelection() const;              // the highlight the selection is in, 0 when none
    int addNoteToSelection(const QString& note);   // highlights it (yellow) when it isn't yet
    void setNote(int annotationId, const QString& note);
    void editNote(int annotationId);               // asks for the text (interactive only)
    bool toggleBookmark();
    // A bookmark on the selected word (its first word), or off again when it has one. Returns its id (0 when removed).
    int toggleWordBookmark();
    int wordBookmarkAtSelection() const;                         // true when the page is bookmarked now
    bool pageIsBookmarked() const;
    void goToAnnotation(int annotationId);
    bool exportNotebook(const QString& path, QString* error);
    NotebookPanel* notebook() const { return m_notebook; }
    bool notebookVisible() const;
    void setNotebookVisible(bool on);
    void refreshAnnotations();

    // The bilingual view: the English of the paragraphs on the screen, in a panel on the right.
    bool bilingualVisible() const;
    void setBilingualVisible(bool on);
    void refreshBilingual();
    BilingualPanel* bilingualPanel() const { return m_bilingual; }

    // The practice workspace on the right (Ctrl+P or the Practice button): flashcards and the words of this page,
    // this book or all books, beside the page. One panel on the right at a time.
    void setPracticeView(PracticeView* view);
    PracticePanel* practicePanel() const { return m_practicePanel; }
    bool practiceVisible() const;
    void setPracticeVisible(bool on);
    // Two pages side by side, or one (the header's button): the Columns setting, Two or One.
    void toggleTwoPages();

    // Search This Book.
    SearchPanel* searchPanel() const { return m_search; }
    void openSearch(const QString& query = QString());
    void closeSearch();
    void goToSearchHit(int index);
    TranslationPopup* translationPopup() const { return m_translate; }
    DictionaryPopup* dictionaryPopup() const { return m_dictCard; }
    // Dictionary (right-click): the selected word's whole entry, as a dictionary card.
    void showDictionary();
    // The pop-ups' Highlight button: the looked-up words highlighted in the color used last, or not any more.
    void toggleLookupHighlight();
    void updateLookupHighlight();
    void translateSelection(bool dictionaryFirst = false);
    // Explain This Sentence: the selection's sentence in English with notes on its grammar and idioms.
    void explainSelection();
    QMenu* buildSelectionMenu();          // the menu for the current selection (the caller shows or deletes it)
    void searchWeb(bool wikipedia);
    QString lastOpenedUrl() const { return m_lastUrl; }   // tests: the browser is never opened when not interactive
    bool bookIsOffline() const;
    void setBookOffline(bool on);
    // Pages right to left (manga): as the book says, unless chosen by hand for this book (kept in the settings).
    bool pagesRightToLeft() const;
    void setPagesRightToLeft(bool on);

signals:
    void rightPanelOpened();   // the Notebook, English alongside or the practice workspace (one panel on the right)
    void libraryRequested();
    void fullScreenRequested();
    void imageRequested(const QString& path);
    void settingsLinkClicked();
    void themeChosen(const QString& key);
    void appearanceChanged();      // the All Books values changed (the settings file needs saving)
    void moreThemesRequested(const QPoint& windowPos);   // below-right corner of the button, window coordinates
    void translationSettingsRequested();
    void wordLogged();
    void fullPracticeRequested();   // the workspace's Open Full Window

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    void onPositionChanged(const core::Position& p, bool byTurn);
    void onIndexReady();
    void updateHeaderAndFooter();
    void onLink(const LinkClick& link, const QPoint& globalPos);
    void savePosition();
    void placeBackChip();
    QString chapterTitleAt(const core::Position& p) const;
    void onTranslated(int id, const core::TranslationResult& r, const QString& why);
    core::WordPlace wordPlace(int spine, int start, int end, const QString& sentence) const;
    // A new highlight of a word or short phrase joins the word log (no lookup); returns the word's id or 0.
    int markHighlightedWord(const QString& text, int spine, int start, int end, const QString& sentence);
    QList<QLocale> speechLocales();

public:
    void speak(const QString& text, const QString& language);
    // Read Aloud (the selection's right-click menu and a command): the selected words in the book's language. A
    // missing voice is said in a notice on the page instead.
    void readSelectionAloud();
    void stopReadingAloud();
    bool isReadingAloud() const;
    QString lastReadAloud() const { return m_lastReadAloud; }   // tests: what was handed to the speech engine
    // Why the word can't be read aloud in `language` (no voice for it, no speech engine), empty when it can.
    QString speechProblem(const QString& language, bool* canOpenSettings = nullptr);
    void setSpeechLocalesForTests(const QList<QLocale>& locales) { m_speechLocalesForTests = locales; }
    // The voices installed on this computer (Settings > Audio lists them by language).
    struct SpeechVoice {
        QString name;
        QLocale locale;
    };
    QList<SpeechVoice> speechVoices();
    void setSpeechVoicesForTests(const QList<SpeechVoice>& voices) { m_speechVoicesForTests = voices; }
    // The voice chosen in Settings > Audio for a language ("es", "en-US"); empty: the system's choice.
    QString chosenVoice(const QString& language) const;
    // Plays a short sentence in the language with the chosen voice, speed and volume. Why it can't, or empty.
    QString playVoiceSample(const QString& language);
    // Tests: what the last speech asked the engine for (also when the engine isn't started).
    struct SpokenWith {
        QString text, language, voice;
        double rate = 0, volume = 1;
    };
    SpokenWith lastSpoken() const { return m_lastSpoken; }
    // Reading statistics: the clock (milliseconds) reading time is measured with (tests use their own).
    void setReadingClockForTests(std::function<qint64()> clock) { m_readingClock = std::move(clock); }
    void flushReading();   // writes the reading time and pages counted so far

    // The paragraphs on screen, from the top of the screen (English alongside translates these).
    struct ParagraphRef {
        int spine = -1, start = 0;
        QString text;
    };
    QList<ParagraphRef> visibleParagraphs() const;

private:
    QString paragraphKey(const QString& text) const;
    void makeRoom(bool right, int width);   // closes the other side's panel when both won't fit beside the page
    void onBilingualTranslated(int id, const core::TranslationResult& r, const QString& why);
    BilingualPanel* m_bilingual = nullptr;
    QTimer m_bilingualTimer;
    QHash<int, QStringList> m_bilingualAsked;   // requests on their way: id -> paragraphs asked

    qint64 readingNow() const;
    void creditReading(bool forwardTurn, int chars);
    std::function<qint64()> m_readingClock;
    std::unique_ptr<core::ReadingLog> m_readingLog;
    qint64 m_readingMark = 0, m_pendingReadMs = 0, m_pendingChars = 0;
    int m_pendingPages = 0;

private:
    std::optional<QList<QLocale>> m_speechLocalesForTests;
    std::optional<QList<SpeechVoice>> m_speechVoicesForTests;
    SpokenWith m_lastSpoken;

    core::BookEngine* m_engine;
    core::Library* m_library;
    Settings& m_settings;
    Theme m_theme = Theme::midnight();
    int m_bookId = 0;
    bool m_interactive = true;

    QToolButton* m_libraryButton;
    QLabel* m_title;
    QLabel* m_subtitle;
    QPushButton* m_notKept;
    QToolButton* m_tocButton;
    QToolButton* m_fullButton;
    QWidget* m_themeDots;
    QToolButton* m_aaButton;
    AppearancePanel* m_panel;
    QTimer m_lookTimer;
    TocPanel* m_toc;
    PageView* m_pages;
    FooterBar* m_footer;
    QPushButton* m_backChip;
    NotePopup* m_note;
    TranslationPopup* m_translate;
    DictionaryPopup* m_dictCard;
    QList<core::OfflineDictionary*> m_dictionaries;
    core::TranslationService* m_translator = nullptr;
    core::WordLog* m_words = nullptr;
    int m_lookupSpine = -1, m_lookupStart = 0, m_lookupEnd = 0;
    QString m_lookupSentence;
    QString m_lookupBookSentence;   // the sentence as the book has it, for the Dictionary card's first side
    bool m_lookupIsWord = false;
    core::OfflineTranslator* m_offlineTranslator = nullptr;
    int m_lookupId = 0;
    bool m_dictionaryFirst = false;
    bool m_keepPopup = false;   // clearing the selection for a highlight leaves the translation open
    void clearSelectionKeepingPopup();
    core::TranslationRequest::Kind m_lookupKind = core::TranslationRequest::Kind::Lookup;
    QString m_lastUrl;
    QTextToSpeech* m_speech = nullptr;
    QString m_lastReadAloud;
    core::Annotations* m_notes = nullptr;
    NotebookPanel* m_notebook;
    SearchPanel* m_search;
    QToolButton* m_searchButton;
    QToolButton* m_bookmarkButton;
    QToolButton* m_notebookButton;
    QToolButton* m_spreadButton;
    QToolButton* m_practiceButton;
    PracticePanel* m_practicePanel;
    void updatePracticePage();   // the pages showing, for the workspace's This Page
    void syncSpreadButton();
    int m_searchId = 0;

    std::vector<core::Position> m_history;
    QTimer m_saveTimer;
    QElapsedTimer m_sinceTurn;
    core::Position m_lastShown;
    int m_lastScreenChars = 0;
};

// The reading screen: header (back to the library, title and chapter, contents, text and layout, themes, full screen), the
// contents panel, the pages, the progress footer, Back after a jump, and footnote pop-ups.
#pragma once

#include "PageView.h"
#include "Theme.h"

#include "core/position/Position.h"

#include <QDateTime>
#include <QJsonValue>
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
class SearchPanel;
class TranslationPopup;
class QMenu;
class QTextToSpeech;
struct Settings;

namespace core {
class BookEngine;
class Library;
class TranslationService;
class WordLog;
class Annotations;
struct SearchHit;
struct TranslationResult;
}

class ReaderView : public QWidget {
    Q_OBJECT
public:
    ReaderView(core::BookEngine* engine, core::Library* library, Settings& settings, QWidget* parent = nullptr);

    void showBook(int bookId, const core::Position& start);
    void closeBook();            // saves the position
    void applyReadingOptions();  // the appearance in effect for this book (All Books, its language, the book)
    Appearance effectiveAppearance() const;
    // A change from the Aa panel or a command, written to the panel's current scope.
    void changeAppearance(const QString& key, const QJsonValue& value);
    AppearancePanel* appearancePanel() const { return m_panel; }
    void showAppearancePanel();
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
    int bookId() const { return m_bookId; }

    // Highlights, notes, bookmarks (phase 4).
    void setAnnotations(core::Annotations* a) { m_notes = a; }
    int highlightSelection(const QString& color);   // the highlight's id; an existing one changes color
    bool removeHighlightAtSelection();
    int highlightAtSelection() const;              // the highlight the selection is in, 0 when none
    int addNoteToSelection(const QString& note);   // highlights it (yellow) when it isn't yet
    void setNote(int annotationId, const QString& note);
    void editNote(int annotationId);               // asks for the text (interactive only)
    bool toggleBookmark();                         // true when the page is bookmarked now
    bool pageIsBookmarked() const;
    void goToAnnotation(int annotationId);
    bool exportNotebook(const QString& path, QString* error);
    NotebookPanel* notebook() const { return m_notebook; }
    bool notebookVisible() const;
    void setNotebookVisible(bool on);
    void refreshAnnotations();

    // Search This Book.
    SearchPanel* searchPanel() const { return m_search; }
    void openSearch(const QString& query = QString());
    void closeSearch();
    void goToSearchHit(int index);
    TranslationPopup* translationPopup() const { return m_translate; }
    void translateSelection(bool dictionaryFirst = false);
    QMenu* buildSelectionMenu();          // the menu for the current selection (the caller shows or deletes it)
    void searchWeb(bool wikipedia);
    QString lastOpenedUrl() const { return m_lastUrl; }   // tests: the browser is never opened when not interactive
    bool bookIsOffline() const;
    void setBookOffline(bool on);

signals:
    void libraryRequested();
    void fullScreenRequested();
    void imageRequested(const QString& path);
    void settingsLinkClicked();
    void themeChosen(const QString& key);
    void appearanceChanged();      // the All Books values changed (the settings file needs saving)
    void moreThemesRequested(const QPoint& windowPos);   // below-right corner of the button, window coordinates
    void translationSettingsRequested();
    void wordLogged();

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
    void speak(const QString& text, const QString& language);

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
    core::TranslationService* m_translator = nullptr;
    core::WordLog* m_words = nullptr;
    int m_lookupSpine = -1, m_lookupStart = 0, m_lookupEnd = 0;
    QString m_lookupSentence;
    int m_lookupId = 0;
    bool m_dictionaryFirst = false;
    QString m_lastUrl;
    QTextToSpeech* m_speech = nullptr;
    core::Annotations* m_notes = nullptr;
    NotebookPanel* m_notebook;
    SearchPanel* m_search;
    QToolButton* m_searchButton;
    QToolButton* m_bookmarkButton;
    QToolButton* m_notebookButton;
    int m_searchId = 0;

    std::vector<core::Position> m_history;
    QTimer m_saveTimer;
    QElapsedTimer m_sinceTurn;
    core::Position m_lastShown;
    int m_lastScreenChars = 0;
};

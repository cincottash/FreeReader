// What the touch app's screens work with: the library (adding books from the phone, opening them), the open book
// and its position, word lookups (Gemini, the Word List, the offline dictionaries) that join the word log as on
// the desktop, and the settings (text size, margins, theme, Gemini key, the dictionary download).
#pragma once

#include "app/Settings.h"
#include "app/Theme.h"
#include "core/layout/PageRenderer.h"
#include "core/services/BookSearch.h"
#include "core/practice/Session.h"
#include "core/position/Position.h"
#include "core/translate/Translation.h"

#include <QAbstractListModel>
#include <QColor>
#include <QImage>
#include <QObject>
#include <QQmlEngine>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <QMargins>
#include <QVariantMap>

#include <memory>
#include <vector>

class BookPage;
class QNetworkAccessManager;
class QTextToSpeech;

namespace core {
class BookEngine;
class DictionaryDownload;
class GeminiProvider;
class Importer;
class Annotations;
class Library;
class LibrarySearch;
class OfflineDictionary;
class SavedWords;
class TranslationService;
class WordLog;
struct ImportedBook;
}

// The books, most recently read first.
class BookListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { IdRole = Qt::UserRole + 1, TitleRole, AuthorRole, ProgressRole, HasCoverRole, LanguageRole, PageRole };
    explicit BookListModel(core::Library* library, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void refresh();

private:
    core::Library* m_library;
    struct Row {
        int id = 0;
        QString title, author, language, page;
        double progress = 0;
        bool hasCover = false;
    };
    QList<Row> m_rows;
};

class MobileApp : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The app object is given to QML by main().")
    Q_PROPERTY(QAbstractItemModel* books READ books CONSTANT)
    Q_PROPERTY(int bookCount READ bookCount NOTIFY booksChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(bool reading READ isReading NOTIFY readingChanged)
    Q_PROPERTY(QString bookTitle READ bookTitle NOTIFY readingChanged)
    Q_PROPERTY(QString chapterTitle READ chapterTitle NOTIFY locationChanged)
    Q_PROPERTY(QString pageText READ pageText NOTIFY locationChanged)
    // The time or the pages left in the chapter (tap to switch, as on the desktop).
    Q_PROPERTY(QString chapterLeft READ chapterLeft NOTIFY locationChanged)
    Q_PROPERTY(double progress READ progress NOTIFY locationChanged)
    Q_PROPERTY(QVariantMap lookup READ lookup NOTIFY lookupChanged)
    Q_PROPERTY(int textSize READ textSize WRITE setTextSize NOTIFY lookChanged)
    Q_PROPERTY(int margins READ margins WRITE setMargins NOTIFY lookChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY lookChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY lookChanged)
    Q_PROPERTY(bool hasGeminiKey READ hasGeminiKey NOTIFY translationChanged)
    Q_PROPERTY(bool keyStoreAvailable READ keyStoreAvailable CONSTANT)
    Q_PROPERTY(QString dictionaryStatus READ dictionaryStatus NOTIFY translationChanged)
    Q_PROPERTY(bool dictionaryInstalled READ dictionaryInstalled NOTIFY translationChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadChanged)
    // Each offline dictionary: {key, title, status, installed, downloading, progress, sizeMb, about}.
    Q_PROPERTY(QVariantList dictionaries READ dictionaries NOTIFY downloadChanged)
    // Every theme: {key, name, group, bg, fg, accent}.
    Q_PROPERTY(QVariantList themes READ themes CONSTANT)
    // The Dictionary card: {found, title, html, source, url, offer}; empty when closed.
    Q_PROPERTY(QVariantMap card READ card NOTIFY cardChanged)
    // Highlights, notes and bookmarks of the open book, in reading order:
    // {id, kind ("highlight"/"bookmark"), color, swatch, text, note, page, chapter}.
    Q_PROPERTY(QVariantList notebook READ notebook NOTIFY annotationsChanged)
    Q_PROPERTY(bool pageBookmarked READ pageBookmarked NOTIFY bookmarkChanged)
    Q_PROPERTY(QVariantList highlightColors READ highlightColors NOTIFY highlightColorsChanged)   // {key, swatch}, most recently used first
    // The book's contents: {title, depth, current}.
    Q_PROPERTY(QVariantList contents READ contents NOTIFY contentsChanged)
    // Search This Book: {before, match, after, page}; `searching` while it runs.
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)
    Q_PROPERTY(bool searching READ isSearching NOTIFY searchChanged)
    Q_PROPERTY(QString searchSummary READ searchSummary NOTIFY searchChanged)
    Q_PROPERTY(QVariantList allResults READ allResults NOTIFY allSearchChanged)
    Q_PROPERTY(bool searchingAll READ isSearchingAll NOTIFY allSearchChanged)
    Q_PROPERTY(QString allSummary READ allSummary NOTIFY allSearchChanged)
    // The Word List: {id, headword, text, translation, partOfSpeech, sentence, book, page, starred, needsMeaning}.
    Q_PROPERTY(QVariantList words READ words NOTIFY wordsChanged)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY wordsChanged)
    // Practice: {active, direction, front, frontDetail, frontSentence, frontIsLong, back, backDetail, backSentence,
    // source, needsMeaning, revealed, progress, intervals [again, hard, good, easy], done, summary, dueNow, newAvailable}.
    Q_PROPERTY(QVariantMap practice READ practice NOTIFY practiceChanged)
    // The order the cards come in: random, alphabetical, newest, oldest, book (every card is practised).
    Q_PROPERTY(QString practiceOrder READ practiceOrder WRITE setPracticeOrder NOTIFY practiceChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    // The file picker's filters: every format FreeReader opens, then all files (a picked file without a name
    // ending is recognised from its bytes).
    Q_PROPERTY(QStringList bookFilters READ bookFilters CONSTANT)
    // The open book's pages turn right to left (manga): taps and swipes are mirrored.
    Q_PROPERTY(bool rightToLeft READ rightToLeft WRITE setRightToLeft NOTIFY directionChanged)
    // The room the phone's status and navigation bars take (Android 15 draws apps under them).
    Q_PROPERTY(int safeTop READ safeTop NOTIFY insetsChanged)
    Q_PROPERTY(int safeBottom READ safeBottom NOTIFY insetsChanged)
    Q_PROPERTY(int safeLeft READ safeLeft NOTIFY insetsChanged)
    Q_PROPERTY(int safeRight READ safeRight NOTIFY insetsChanged)
    // The settings the screens show and change, by name (setOption): useGemini, geminiExplainOnly, geminiModel,
    // geminiWaitSec, useDictionary, bookOffline (the open book), practiceHighlights, practiceLookups, practiceTyped,
    // practiceSentenceFront, practicePassages, practiceDirections.
    Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)
    // Hear Pronunciation or Read Aloud is speaking (the button then stops it).
    Q_PROPERTY(bool speaking READ isSpeaking NOTIFY speakingChanged)
    // The open book's id (0: none), so a reader screen closes only its own book.
    Q_PROPERTY(int bookId READ bookId NOTIFY readingChanged)
    // After a jump (contents, notebook, search, a link, Go to Page): "Back to Page 7"; empty when there is nowhere
    // to go back to.
    Q_PROPERTY(QString backText READ backText NOTIFY historyChanged)
    // The picture tapped on the page (image://book/<n>), empty when none is shown.
    Q_PROPERTY(QString shownImage READ shownImage NOTIFY imageChanged)
public:
    MobileApp(const QString& dataFolder, QObject* parent = nullptr);
    ~MobileApp() override;
    bool isOpen() const { return m_library != nullptr; }

    QAbstractItemModel* books() const;
    int bookCount() const;
    QString message() const { return m_message; }
    bool isBusy() const { return m_busy; }
    bool isReading() const { return m_bookId > 0; }
    QString bookTitle() const { return m_title; }
    QString chapterTitle() const { return m_chapter; }
    QString pageText() const { return m_pageText; }
    QString chapterLeft() const { return m_chapterLeft; }
    Q_INVOKABLE void toggleChapterLeft();
    double progress() const { return m_progress; }
    QVariantMap lookup() const { return m_lookup; }

    // Adding a book: the file is copied into the app's own folder (a phone's file picker gives access only for a
    // while), then read like on the desktop.
    Q_INVOKABLE void importBook(const QUrl& url);
    Q_INVOKABLE void openBook(int id);
    Q_INVOKABLE void closeBook();
    Q_INVOKABLE void removeBook(int id);   // from the library, and the app's copy of the file

    // Looks up the page's selection (a word, a phrase or a sentence); the answer arrives in `lookup`.
    Q_INVOKABLE void lookUp(QObject* page);   // a BookPage
    Q_INVOKABLE void closeLookup();
    Q_INVOKABLE void explain(QObject* page);   // Explain This Sentence: the selection's sentence, with notes
    Q_INVOKABLE void pageMoved(QObject* page);   // a BookPage   // the reader turned a page: update and save the position

    // Moving around (as on the desktop): every jump can be undone with Back.
    QString backText() const;
    Q_INVOKABLE void goBack();
    Q_INVOKABLE QString goToPageOrPercent(const QString& text);   // empty when it went there, else what to type
    Q_INVOKABLE QString pageOrPercentHint() const;                 // "Page 1 to 1,180, or a percentage such as 45%"
    Q_INVOKABLE void nextChapter();
    Q_INVOKABLE void previousChapter();   // the start of this chapter, or of the one before at its start
    Q_INVOKABLE void goToBeginning();
    // A link on the page: a footnote shows its text (footnoteShown), another place in the book is jumped to, a web
    // address opens in the browser.
    void followLink(const QString& target, bool external, bool noteRef);
    Q_INVOKABLE void goToFootnote();   // the footnote's own place in the book
    void showImage(const QString& path);
    Q_INVOKABLE void closeImage();
    QString shownImage() const { return m_imageShown; }
    QImage imageForProvider() const { return m_image; }
    // The provider of image://book/ (pictures from the open book); the QML engine takes it.
    class QQuickImageProvider* newImageProvider();

    // Settings
    int textSize() const { return m_settings.look.textSizePct; }
    void setTextSize(int pct);
    int margins() const { return m_settings.look.marginPx; }
    void setMargins(int px);
    QString theme() const { return m_settings.theme; }
    void setTheme(const QString& key);
    QVariantMap colors() const;
    bool hasGeminiKey() const;
    bool keyStoreAvailable() const;
    Q_INVOKABLE QString saveGeminiKey(const QString& key);   // empty when saved, else what went wrong
    Q_INVOKABLE void removeGeminiKey();
    QString dictionaryStatus() const;
    bool dictionaryInstalled() const;
    double downloadProgress() const { return m_downloadProgress; }
    Q_INVOKABLE void downloadDictionary(const QString& key = QStringLiteral("es"));
    Q_INVOKABLE void cancelDownload(const QString& key = QString());   // empty: every running download
    Q_INVOKABLE void removeDictionary(const QString& key);
    QVariantList dictionaries() const;
    QVariantList themes() const;

    // The Dictionary card for the page's selection; false when no dictionary knows the word (then look it up).
    Q_INVOKABLE bool showDictionary(QObject* page);
    Q_INVOKABLE void closeCard();
    Q_INVOKABLE void flipCard();   // the card's other side (the book's language, or the reader's)
    QVariantMap card() const { return m_card; }

    // Highlights, notes and bookmarks (the same rules as the desktop: whole words, merged, the same color again
    // takes a highlight away). `page`: a BookPage with a selection.
    Q_INVOKABLE int highlight(QObject* page, const QString& color);
    Q_INVOKABLE QString highlightColorAt(QObject* page) const;   // the selection's highlight color, empty if none
    Q_INVOKABLE QString noteAt(QObject* page) const;
    Q_INVOKABLE int setNote(QObject* page, const QString& note);  // highlights in yellow when it isn't yet
    Q_INVOKABLE void removeHighlightAt(QObject* page);
    Q_INVOKABLE void toggleBookmark();
    // A bookmark on the selected word (not the page); off again when it has one.
    Q_INVOKABLE void toggleWordBookmark(QObject* page);
    Q_INVOKABLE bool wordBookmarkAt(QObject* page) const;
    std::vector<std::pair<int, int>> wordBookmarksIn(int spine) const;   // for painting
    Q_INVOKABLE void removeAnnotation(int id);
    Q_INVOKABLE void goToAnnotation(int id);
    Q_INVOKABLE void copyText(const QString& text);
    QVariantList notebook() const;
    bool pageBookmarked() const;
    QVariantList highlightColors() const;
    std::vector<layout::Overlay> highlightsIn(int spine) const;   // for painting
    core::Annotations* annotations() const { return m_notes.get(); }

    // Contents and search.
    QVariantList contents() const;
    Q_INVOKABLE void goToContents(int index);
    Q_INVOKABLE void search(const QString& query, bool wholeWord = false, bool matchCase = false, bool matchAccents = false);
    Q_INVOKABLE void goToSearchResult(int index);
    // Search All Books: rows of {kind "book", title, author, count} each followed by its {kind "hit", before, match,
    // after}; a tap on a hit opens its book there.
    Q_INVOKABLE void searchAll(const QString& query, bool wholeWord = false, bool matchCase = false, bool matchAccents = false);
    Q_INVOKABLE void cancelSearchAll();
    Q_INVOKABLE void openAllResult(int row);
    QVariantList allResults() const;
    bool isSearchingAll() const { return m_allSearchId > 0; }
    QString allSummary() const { return m_allSummary; }
    QVariantList searchResults() const;
    bool isSearching() const { return m_searchId > 0; }
    QString searchSummary() const { return m_searchSummary; }

    // Word List
    QVariantList words() const;
    int wordCount() const;
    Q_INVOKABLE void removeWord(int id);
    Q_INVOKABLE void setWordStarred(int id, bool on);
    Q_INVOKABLE void setWordMeaning(int id, const QString& meaning);
    // Practice (flashcards, the same cards and schedule as the desktop)
    QVariantMap practice() const;
    Q_INVOKABLE void refreshPractice();   // the counts on the start screen
    Q_INVOKABLE bool startPractice();
    Q_INVOKABLE void revealCard();
    Q_INVOKABLE void checkTyped(const QString& typed);   // Type the Answer: checks it, then shows the card's back
    Q_INVOKABLE QVariantList wordBooks() const;           // {id, title} of the books the Word List has words from
    Q_INVOKABLE void startOverWord(int id);               // Start Over: the word's cards become new again
    Q_INVOKABLE void removeEarlierBookmarks(bool all);    // the answer to bookmarkQuestion
    Q_INVOKABLE void goToLatestBookmark();                // the bookmark added last, in any book
    Q_INVOKABLE bool hasBookmarks() const;
    Q_INVOKABLE void showWordInBook(int id);              // Show in Book: the word's place, its book opened if needed
    int bookId() const { return m_bookId; }
    Q_INVOKABLE void rateCard(int rating);   // 1 Again, 2 Hard, 3 Good, 4 Easy
    Q_INVOKABLE void endPractice();
    QString practiceOrder() const { return m_settings.practiceOrder; }
    void setPracticeOrder(const QString& order);
    Q_INVOKABLE void openLink(const QString& url);
    QVariantMap options() const;
    Q_INVOKABLE void setOption(const QString& key, const QVariant& value);
    Q_INVOKABLE void testGemini();   // looks up one word with the saved key; geminiTested says how it went
    bool bookOffline() const;
    // Hear Pronunciation (a word, in the book's language) and Read Aloud (the selection).
    Q_INVOKABLE void hear(const QString& text);
    Q_INVOKABLE void readAloud(QObject* page);
    Q_INVOKABLE void stopSpeaking();
    bool isSpeaking() const;
    QString lastSpoken() const { return m_lastSpoken; }   // tests
    QString lastOpened() const { return m_lastOpened; }   // tests: the last web address opened
    void setQuietForTests(bool on) { m_quiet = on; }       // never start the system's speech engine or browser
    // Report an AI's answer (Google Play's AI-generated content policy): opens a new GitHub issue, filled in.
    Q_INVOKABLE void reportAnswer();
    QUrl reportUrl() const;   // that issue's address (tests read it without opening a browser)
    QString version() const;
    QStringList bookFilters() const;
    bool rightToLeft() const;
    void setRightToLeft(bool on);
    int safeTop() const { return m_insets.top(); }
    int safeBottom() const { return m_insets.bottom(); }
    int safeLeft() const { return m_insets.left(); }
    int safeRight() const { return m_insets.right(); }
    Q_INVOKABLE void updateInsets();   // after the window is shown and when it turns

    // For the reading surface.
    struct Colors {
        QColor bg, fg, muted, accent, line, surface, sel, pop;
        bool dark = false;
    };
    Colors pageColors() const;
    core::BookEngine* engine() const { return m_engine; }
    core::Library* library() const { return m_library.get(); }
    core::WordLog* wordLog() const { return m_words.get(); }
    Appearance appearance() const { return m_settings.look; }
    core::Position startPosition() const { return m_start; }
    QString languageCode() const;
    bool waitUntilOpen(int ms);   // tests and the screenshot mode

signals:
    void booksChanged();
    void directionChanged();
    void messageChanged();
    void busyChanged();
    void readingChanged();
    void locationChanged();
    void lookupChanged();
    void lookChanged();
    void translationChanged();
    void downloadChanged();
    void bookOpened();
    void insetsChanged();
    void cardChanged();
    void annotationsChanged();
    void highlightColorsChanged();
    void bookmarkChanged();   // the page moved, or its bookmark changed
    void contentsChanged();
    void searchChanged();
    void jumpRequested(int spine, int offset);
    void selectRequested(int spine, int start, int end);   // after a jump: the words to show (a highlight, a word bookmark)
    void wordsChanged();
    void practiceChanged();   // the reader goes there (contents, notebook, search)
    void optionsChanged();
    void geminiTested(const QString& result);
    void speakingChanged();
    // A bookmark was added where the book has others: remove the earlier one(s)? (count: how many there are)
    void bookmarkQuestion(const QString& question, int count);
    void historyChanged();
    void allSearchChanged();
    void footnoteShown(const QString& text);
    void imageChanged();

private:
    void setMessage(const QString& text);
    void applyTranslationSettings();
    void speak(const QString& text, const QString& language);
    void startSpeechEngine();   // Android's engine starts in the background: early, when a book opens
    void sayNow(const QString& text, const QString& language);
    void onOpened();
    void onTranslated(int id, const core::TranslationResult& r, const QString& why);
    void savePosition();
    void updateLocation();   // chapter, page and progress of m_position
    void saveSettings();
    QString chapterTitleAt(const core::Position& p) const;
    QString dictionaryFile(const QString& key = QStringLiteral("es")) const;
    core::OfflineDictionary* dictionaryFor(const QString& key) const;
    int annotationAt(QObject* page) const;   // the highlight holding the page's selection, 0 if none
    void jumpTo(const core::Position& p);    // remembers where the reader was (Back)
    void forgetHistory();

    QString m_folder;
    Settings m_settings;
    std::unique_ptr<core::Library> m_library;
    std::unique_ptr<core::WordLog> m_words;
    BookListModel* m_model = nullptr;
    core::BookEngine* m_engine;
    core::Importer* m_importer;
    QNetworkAccessManager* m_net;
    core::GeminiProvider* m_gemini;
    core::SavedWords* m_savedWords;
    core::OfflineDictionary* m_dictionary;     // Spanish -> English
    core::OfflineDictionary* m_dictionaryEn;   // English definitions
    core::OfflineDictionary* m_definitionsEs;  // Spanish definitions (the Dictionary card only)
    QHash<QString, core::DictionaryDownload*> m_downloads;
    QHash<QString, double> m_progressByKey;
    std::unique_ptr<core::Annotations> m_notes;
    QVariantMap m_card;
    int m_searchId = 0;
    QList<core::SearchHit> m_hits;
    QString m_searchSummary;
    std::unique_ptr<core::practice::Session> m_session;
    bool m_revealed = false;
    QString m_practiceSummary;
    core::TranslationService* m_translator;
    double m_downloadProgress = -1;

    QString m_message;
    QMargins m_insets;
    bool m_busy = false;
    QString m_openingPath;
    int m_openingId = 0;
    int m_bookId = 0;
    QString m_title, m_chapter, m_pageText, m_chapterLeft;
    QElapsedTimer m_sinceTurn;
    double m_progress = 0;
    core::Position m_start, m_position;
    QTimer m_saveTimer;

    int m_lookupId = -1;
    QVariantMap m_lookup;
    QTextToSpeech* m_speech = nullptr;   // made on first use
    int m_practiceBook = 0;              // this session's "Words From" (0: every book)
    bool m_practiceStarred = false;      // this session's "Starred Words Only"
    QString m_verdict;                   // Type the Answer: how the typed answer went
    int m_suggested = 0;                 // the rating it suggests
    QList<int> m_earlierBookmarks;       // the book's other bookmarks when one was added (newest first)
    QString m_wordSearch;                // the Word List's search (this session)
    int m_wordBook = 0;                  // the Word List's book (0: all)
    struct PendingWord {                 // Show in Book in a book not open yet: where to go once it is
        int bookId = 0, spine = -1, start = 0, end = 0;
        QString cfi;
    } m_pendingWord;
    core::LibrarySearch* m_librarySearch = nullptr;
    struct AllRow {
        int bookId = 0;
        bool isHit = false;
        QString title, author;   // a book's row
        int count = 0;
        bool truncated = false;
        core::SearchHit hit;     // a match's row
    };
    std::vector<AllRow> m_allRows;
    int m_allSearchId = 0, m_allUnreadable = 0;
    QString m_allSummary, m_allQuery;
    std::vector<core::Position> m_history;   // where each jump came from, the last one last
    core::Position m_footnoteTarget{-1, 0};
    QImage m_image;
    QString m_imageShown;
    int m_imageSerial = 0;
    QString m_lastSpoken, m_lastOpened;
    QPair<QString, QString> m_pendingSpeech;   // text and language waiting for the engine to be ready
    bool m_quiet = false;
    struct Asked {
        int spine = -1, start = 0, end = 0;
        QString sentence;
    } m_asked;
};

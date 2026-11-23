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
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QMargins>
#include <QVariantMap>

#include <memory>
#include <vector>

class BookPage;
class QNetworkAccessManager;

namespace core {
class BookEngine;
class DictionaryDownload;
class GeminiProvider;
class Importer;
class Annotations;
class Library;
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
    Q_PROPERTY(QVariantList highlightColors READ highlightColors CONSTANT)   // {key, swatch}
    // The book's contents: {title, depth, current}.
    Q_PROPERTY(QVariantList contents READ contents NOTIFY contentsChanged)
    // Search This Book: {before, match, after, page}; `searching` while it runs.
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)
    Q_PROPERTY(bool searching READ isSearching NOTIFY searchChanged)
    Q_PROPERTY(QString searchSummary READ searchSummary NOTIFY searchChanged)
    // The Word List: {id, headword, text, translation, partOfSpeech, sentence, book, page, starred, needsMeaning}.
    Q_PROPERTY(QVariantList words READ words NOTIFY wordsChanged)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY wordsChanged)
    // Practice: {active, direction, front, frontDetail, frontSentence, frontIsLong, back, backDetail, backSentence,
    // source, needsMeaning, revealed, progress, intervals [again, hard, good, easy], done, summary, dueNow, newAvailable}.
    Q_PROPERTY(QVariantMap practice READ practice NOTIFY practiceChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    // The room the phone's status and navigation bars take (Android 15 draws apps under them).
    Q_PROPERTY(int safeTop READ safeTop NOTIFY insetsChanged)
    Q_PROPERTY(int safeBottom READ safeBottom NOTIFY insetsChanged)
    Q_PROPERTY(int safeLeft READ safeLeft NOTIFY insetsChanged)
    Q_PROPERTY(int safeRight READ safeRight NOTIFY insetsChanged)
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
    Q_INVOKABLE void pageMoved(QObject* page);   // a BookPage   // the reader turned a page: update and save the position

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
    QVariantMap card() const { return m_card; }

    // Highlights, notes and bookmarks (the same rules as the desktop: whole words, merged, the same color again
    // takes a highlight away). `page`: a BookPage with a selection.
    Q_INVOKABLE int highlight(QObject* page, const QString& color);
    Q_INVOKABLE QString highlightColorAt(QObject* page) const;   // the selection's highlight color, empty if none
    Q_INVOKABLE QString noteAt(QObject* page) const;
    Q_INVOKABLE int setNote(QObject* page, const QString& note);  // highlights in yellow when it isn't yet
    Q_INVOKABLE void removeHighlightAt(QObject* page);
    Q_INVOKABLE void toggleBookmark();
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
    Q_INVOKABLE void search(const QString& query);
    Q_INVOKABLE void goToSearchResult(int index);
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
    Q_INVOKABLE void rateCard(int rating);   // 1 Again, 2 Hard, 3 Good, 4 Easy
    Q_INVOKABLE void endPractice();
    Q_INVOKABLE void openLink(const QString& url);
    QString version() const;
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
    void bookmarkChanged();   // the page moved, or its bookmark changed
    void contentsChanged();
    void searchChanged();
    void jumpRequested(int spine, int offset);
    void wordsChanged();
    void practiceChanged();   // the reader goes there (contents, notebook, search)

private:
    void setMessage(const QString& text);
    void onOpened();
    void onTranslated(int id, const core::TranslationResult& r, const QString& why);
    void savePosition();
    void updateLocation();   // chapter, page and progress of m_position
    void saveSettings();
    QString chapterTitleAt(const core::Position& p) const;
    QString dictionaryFile(const QString& key = QStringLiteral("es")) const;
    core::OfflineDictionary* dictionaryFor(const QString& key) const;
    int annotationAt(QObject* page) const;   // the highlight holding the page's selection, 0 if none

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
    QString m_title, m_chapter, m_pageText;
    double m_progress = 0;
    core::Position m_start, m_position;
    QTimer m_saveTimer;

    int m_lookupId = -1;
    QVariantMap m_lookup;
    struct Asked {
        int spine = -1, start = 0, end = 0;
        QString sentence;
    } m_asked;
};

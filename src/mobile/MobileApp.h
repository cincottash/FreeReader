// What the touch app's screens work with: the library (adding books from the phone, opening them), the open book
// and its position, word lookups (Gemini, the Word List, the offline dictionaries) that join the word log as on
// the desktop, and the settings (text size, margins, theme, Gemini key, the dictionary download).
#pragma once

#include "app/Settings.h"
#include "core/position/Position.h"
#include "core/translate/Translation.h"

#include <QAbstractListModel>
#include <QColor>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <memory>

class BookPage;
class QNetworkAccessManager;

namespace core {
class BookEngine;
class DictionaryDownload;
class GeminiProvider;
class Importer;
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
    Q_PROPERTY(QString version READ version CONSTANT)
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
    Q_INVOKABLE void downloadDictionary();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void openLink(const QString& url);
    QString version() const;

    // For the reading surface.
    struct Colors {
        QColor bg{0xFA, 0xF7, 0xF0}, fg{0x2B, 0x27, 0x21}, muted{0x7A, 0x72, 0x66}, accent{0xB5, 0x65, 0x1D},
            line{0xDD, 0xD5, 0xC6}, surface{0xF1, 0xEC, 0xE1}, sel{0xF0, 0xC8, 0x78, 0x90}, pop{0xFF, 0xFF, 0xFF};
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

private:
    void setMessage(const QString& text);
    void onOpened();
    void onTranslated(int id, const core::TranslationResult& r, const QString& why);
    void savePosition();
    void updateLocation();   // chapter, page and progress of m_position
    void saveSettings();
    QString chapterTitleAt(const core::Position& p) const;
    QString dictionaryFile() const;

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
    core::OfflineDictionary* m_dictionary;
    core::TranslationService* m_translator;
    core::DictionaryDownload* m_download = nullptr;
    double m_downloadProgress = -1;

    QString m_message;
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

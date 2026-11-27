#include "MobileApp.h"

#include "AndroidSystem.h"
#include "BookPage.h"

#include "core/format/Convert.h"
#include "core/format/FormatPlugin.h"
#include "core/services/Annotations.h"
#include "core/services/BookEngine.h"
#include "core/services/BookIndex.h"
#include "core/services/Highlighting.h"
#include "core/services/Importer.h"
#include "core/services/Library.h"
#include "core/services/LibrarySearch.h"
#include "core/services/Secrets.h"
#include "core/services/WordLog.h"
#include "core/practice/Answer.h"
#include "core/translate/AiProviders.h"
#include "core/translate/DictionaryCardHtml.h"
#include "core/translate/DictionaryDownload.h"
#include "core/translate/OfflineDictionary.h"
#include "core/translate/SavedWords.h"
#include "core/translate/LanguagePacks.h"
#include "core/translate/TranslationService.h"
#include "core/layout/PageTurn.h"

#include <QClipboard>
#include <QCollator>
#include <QCryptographicHash>
#include <QFontDatabase>
#include <QJsonObject>
#include <array>
#include <QDesktopServices>
#include <QUrlQuery>
#include <QGuiApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQuickImageProvider>
#include <cmath>
#ifdef HAVE_TEXT_TO_SPEECH
#include <QTextToSpeech>
#endif
#include <QRegularExpression>

namespace {

const QString kGeminiKey = QStringLiteral("gemini-api-key");

// "1 book", "3 books" (the desktop's way: English has no plural forms file).
QString countText(int n, const QString& one, const QString& many) { return QStringLiteral("%1 %2").arg(QLocale().toString(n), n == 1 ? one : many); }

// The page's colors: the same themes as the desktop app.
MobileApp::Colors themeColors(const QString& key)
{
    const Theme t = Theme::forChoice(key);
    MobileApp::Colors c;
    c.bg = t.bg;
    c.fg = t.fg;
    c.muted = t.muted;
    c.accent = t.accent;
    c.line = t.line;
    c.surface = t.surface;
    c.sel = t.sel;
    c.pop = t.pop;
    c.dark = t.dark;
    return c;
}

bool isThemeKey(const QString& key)
{
    if (key == QLatin1String("system")) return true;
    for (const ThemeDef& d : themeTable())
        if (d.key == key) return true;
    return false;
}

// The offline dictionaries: Spanish -> English, English definitions, Spanish definitions (as on the desktop).
struct DictionaryKind {
    const char* key;
    const char* file;
};
const DictionaryKind kDictionaries[] = {{"es", "es-en.sqlite"}, {"en", "en-en.sqlite"}, {"es-es", "es-es.sqlite"}};

core::DictionaryDownload::Source sourceFor(const QString& key)
{
    if (key == QLatin1String("en")) return core::DictionaryDownload::englishDefinitions();
    if (key == QLatin1String("es-es")) return core::DictionaryDownload::spanishDefinitions();
    return core::DictionaryDownload::spanishEnglish();
}

QString keyOf(const core::DictionaryInfo& info)
{
    if (info.language == QLatin1String("es") && info.target == QLatin1String("en")) return QStringLiteral("es");
    if (info.language == QLatin1String("en") && info.target == QLatin1String("en")) return QStringLiteral("en");
    if (info.language == QLatin1String("es") && info.target == QLatin1String("es")) return QStringLiteral("es-es");
    return {};
}

QString safeFileName(QString name)
{
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return name.trimmed();
}

// Whether a file name ends in a suffix some format opens ("x.epub", "x.fb2.zip").
bool knownSuffix(const QString& name)
{
    const QString lower = name.toLower();
    for (const QString& s : core::FormatRegistry::instance().fileSuffixes())
        if (lower.endsWith(QLatin1Char('.') + s)) return true;
    return false;
}

}  // namespace

// ---------------------------------------------------------------- the book list

BookListModel::BookListModel(core::Library* library, QObject* parent) : QAbstractListModel(parent), m_library(library) { refresh(); }

int BookListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(m_rows.size()); }

QVariant BookListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row& r = m_rows[index.row()];
    switch (role) {
    case IdRole: return r.id;
    case Qt::DisplayRole:
    case TitleRole: return r.title;
    case AuthorRole: return r.author;
    case ProgressRole: return r.progress;
    case HasCoverRole: return r.hasCover;
    case LanguageRole: return r.language;
    case PageRole: return r.page;
    case PageCountRole: return r.pageCount;
    case FinishedRole: return r.finished;
    case MissingRole: return r.missing;
    default: return {};
    }
}

QHash<int, QByteArray> BookListModel::roleNames() const
{
    return {{IdRole, "bookId"}, {TitleRole, "title"}, {AuthorRole, "author"}, {ProgressRole, "progress"},
            {HasCoverRole, "hasCover"}, {LanguageRole, "language"}, {PageRole, "page"}, {PageCountRole, "pageCount"},
            {FinishedRole, "finished"}, {MissingRole, "missing"}};
}

void BookListModel::setView(const QString& filter, const QString& search, const QString& sort)
{
    m_filter = filter.isEmpty() ? QStringLiteral("all") : filter;
    m_search = search;
    m_sort = sort.isEmpty() ? QStringLiteral("recent") : sort;
    refresh();
}

void BookListModel::refresh()
{
    beginResetModel();
    m_rows.clear();
    if (m_library) {
        QList<core::LibraryBook> books = m_library->books();
        const QString search = core::foldForSearch(m_search.simplified(), false, false);
        const QHash<int, QSet<int>> members = m_filter.startsWith(QLatin1String("collection:")) ? m_library->collectionMembers() : QHash<int, QSet<int>>();
        auto shown = [&](const core::LibraryBook& b) {
            if (!search.isEmpty() && !core::foldForSearch(b.title + QLatin1Char(' ') + b.author, false, false).contains(search)) return false;
            const QString lang = b.language.section(QLatin1Char('-'), 0, 0).toLower();
            if (m_filter == QLatin1String("reading")) return b.started() && !b.finished();
            if (m_filter == QLatin1String("finished")) return b.finished();
            if (m_filter == QLatin1String("notstarted")) return !b.started() && !b.finished();
            if (m_filter.startsWith(QLatin1String("lang:"))) return lang == m_filter.mid(5);
            if (m_filter.startsWith(QLatin1String("collection:"))) return members.value(m_filter.mid(11).toInt()).contains(b.id);
            return true;
        };
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        auto recent = [](const core::LibraryBook& b) { return b.lastOpened.isValid() ? b.lastOpened : b.added; };
        std::stable_sort(books.begin(), books.end(), [&](const core::LibraryBook& a, const core::LibraryBook& b) {
            int c = 0;
            if (m_sort == QLatin1String("title")) c = 0;
            else if (m_sort == QLatin1String("author")) c = collator.compare(a.author, b.author);
            else if (m_sort == QLatin1String("language")) c = collator.compare(a.language, b.language);
            else if (m_sort == QLatin1String("progress")) c = a.progress > b.progress ? -1 : a.progress < b.progress ? 1 : 0;
            else if (m_sort == QLatin1String("added")) c = a.added > b.added ? -1 : a.added < b.added ? 1 : 0;
            else c = recent(a) > recent(b) ? -1 : recent(a) < recent(b) ? 1 : 0;
            return c != 0 ? c < 0 : collator.compare(a.title, b.title) < 0;
        });
        for (const core::LibraryBook& b : books)
            if (shown(b))
                m_rows.append({b.id, b.title, b.author, b.language, b.pageLabel, b.pageCount, b.progress, b.hasCover, b.finished(),
                               !QFileInfo::exists(b.path)});
    }
    endResetModel();
}

// ---------------------------------------------------------------- the app

MobileApp::MobileApp(const QString& dataFolder, QObject* parent) : QObject(parent), m_folder(dataFolder)
{
    QDir().mkpath(m_folder);
    // Phone-sized defaults; a saved settings file replaces them.
    m_settings.look.textSizePct = 90;
    m_settings.look.marginPx = 20;
    m_settings.look.lineSpacingPct = 150;
    m_settings.theme = QStringLiteral("paper");
    m_settings.load(QDir(m_folder).filePath(QStringLiteral("settings.json")));
    // The first touch app had three themes of its own; "night" is Midnight now.
    if (m_settings.theme == QLatin1String("night")) m_settings.theme = QStringLiteral("midnight");
    if (!isThemeKey(m_settings.theme)) m_settings.theme = QStringLiteral("paper");

    m_library = std::make_unique<core::Library>();
    QString err;
    if (!m_library->open(QDir(m_folder).filePath(QStringLiteral("library.sqlite")), &err)) {
        m_library.reset();
        setMessage(tr("The library can't be opened: %1").arg(err));
    }
    m_model = new BookListModel(m_library.get(), this);
    if (m_library) m_words = std::make_unique<core::WordLog>(m_library.get());
    if (m_library) m_notes = std::make_unique<core::Annotations>(m_library.get());

    m_engine = new core::BookEngine(this);
    connect(m_engine, &core::BookEngine::opened, this, &MobileApp::onOpened);
    connect(m_engine, &core::BookEngine::openFailed, this, [this](const QString& why) {
        m_openingPath.clear();
        m_openingId = 0;
        setMessage(why);
    });
    connect(m_engine, &core::BookEngine::indexReady, this, [this] {
        updateLocation();
        emit contentsChanged();
    });
    connect(m_engine, &core::BookEngine::searchFinished, this, [this](int id, const QList<core::SearchHit>& hits, bool truncated) {
        if (id != m_searchId) return;
        m_searchId = 0;
        m_hits = hits;
        m_searchSummary = hits.isEmpty() ? tr("Not found.")
                          : truncated   ? tr("The first %1 matches.").arg(hits.size())
                                        : countText(int(hits.size()), tr("match"), tr("matches")) + QLatin1Char('.');
        emit searchChanged();
    });

    // Search All Books: each book's matches as soon as it is searched.
    m_librarySearch = new core::LibrarySearch(this);
    connect(m_librarySearch, &core::LibrarySearch::bookSearched, this,
            [this](int id, const core::LibraryBookHits& r, int done, int total) {
                if (id != m_allSearchId) return;
                if (!r.error.isEmpty()) ++m_allUnreadable;
                m_allSummary = tr("Searching… %1 of %2 books.").arg(QLocale().toString(done), QLocale().toString(total));
                if (!r.hits.isEmpty()) {
                    AllRow head;
                    head.bookId = r.bookId;
                    head.title = r.title.isEmpty() ? QFileInfo(r.path).completeBaseName() : r.title;
                    head.author = r.author;
                    head.count = int(r.hits.size());
                    head.truncated = r.truncated;
                    m_allRows.push_back(head);
                    for (const core::SearchHit& h : r.hits) {
                        AllRow row;
                        row.bookId = r.bookId;
                        row.hit = h;
                        row.isHit = true;
                        m_allRows.push_back(row);
                    }
                }
                emit allSearchChanged();
            });
    connect(m_librarySearch, &core::LibrarySearch::finished, this, [this](int id, int hits, int books, bool canceled) {
        if (id != m_allSearchId) return;
        m_allSearchId = 0;
        const QString results = countText(hits, tr("result"), tr("results")), inBooks = countText(books, tr("book"), tr("books"));
        if (canceled) m_allSummary = tr("Stopped. %1 in %2 so far.").arg(results, inBooks);
        else if (hits == 0) m_allSummary = tr("No results for “%1” in any book.").arg(m_allQuery);
        else m_allSummary = tr("%1 in %2.").arg(results, inBooks);
        if (m_allUnreadable > 0) m_allSummary += QLatin1Char(' ') + tr("%1 couldn't be read.").arg(countText(m_allUnreadable, tr("book"), tr("books")));
        emit allSearchChanged();
    });

    m_importer = new core::Importer(this);
    connect(m_importer, &core::Importer::bookRead, this, [this](const core::ImportedBook& b) {
        const QString title = b.meta.title.isEmpty() ? QFileInfo(b.path).completeBaseName() : b.meta.title;
        // A book already in the library isn't added twice: the app's new copy of it goes again.
        if (m_library && m_library->duplicateOf(b.path, b.meta, b.contentHash)) {
            QFile::remove(b.path);
            setMessage(tr("%1 is already in your library.").arg(title));
            return;
        }
        if (m_library) m_library->addBook(b.path, b.meta, b.coverPng, b.contentHash);
        m_model->refresh();
        emit booksChanged();
        setMessage(tr("Added %1.").arg(title));
    });
    connect(m_importer, &core::Importer::failed, this, [this](const QString& path, const QString& why) {
        QFile::remove(path);   // the app's own copy of a file it can't read
        setMessage(why);
    });
    connect(m_importer, &core::Importer::finished, this, [this] {
        m_busy = false;
        emit busyChanged();
    });

    // Translation: Gemini, then the offline dictionary, then (last) what the Word List already says.
    m_net = new QNetworkAccessManager(this);
    m_gemini = new core::GeminiProvider(m_net, this);
    m_gemini->setKey(core::secrets::read(kGeminiKey));
    m_dictionary = new core::OfflineDictionary(this);
    m_dictionary->open(dictionaryFile(QStringLiteral("es")));
    m_dictionaryEn = new core::OfflineDictionary(this);
    m_dictionaryEn->open(dictionaryFile(QStringLiteral("en")));
    m_definitionsEs = new core::OfflineDictionary(this);   // the Dictionary card only, never a translation
    m_definitionsEs->open(dictionaryFile(QStringLiteral("es-es")));
    m_savedWords = new core::SavedWords(this);
    m_savedWords->setDictionaries({m_dictionary, m_dictionaryEn});
    m_savedWords->setWordLog(m_words.get());
    m_translator = new core::TranslationService(this);
    m_translator->setCache(m_library.get());
    // The Word List last: its meaning was right for another sentence (the owner's rule, 2026-11-27).
    m_translator->setProviders({m_gemini, m_dictionary, m_dictionaryEn, m_savedWords});
    m_translator->setInstant({m_dictionary, m_dictionaryEn});
    // The engines switched on or off, Gemini's model and its wait: a word or passage Gemini hasn't answered by then
    // (2.5 s, as on the desktop) is answered by the next engine; Gemini's late answer is kept for next time (the
    // owner's rule, 2026-11-26).
    applyTranslationSettings();
    // One answer per lookup, from the first engine that can give it (as on the desktop).
    connect(m_translator, &core::TranslationService::finished, this, &MobileApp::onTranslated);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1500);
    connect(&m_saveTimer, &QTimer::timeout, this, &MobileApp::savePosition);
    connect(this, &MobileApp::annotationsChanged, this, &MobileApp::bookmarkChanged);
    m_alongsideTimer.setSingleShot(true);
    m_alongsideTimer.setInterval(250);
    connect(&m_alongsideTimer, &QTimer::timeout, this, &MobileApp::refreshAlongside);
}

MobileApp::~MobileApp()
{
    savePosition();
    saveSettings();
}

QAbstractItemModel* MobileApp::books() const { return m_model; }
int MobileApp::bookCount() const { return m_model->rowCount(); }

void MobileApp::setMessage(const QString& text)
{
    m_message = text;
    emit messageChanged();
}

QString MobileApp::version() const { return QStringLiteral(APP_VERSION); }

void MobileApp::updateInsets()
{
    android_system::setLightSystemBars(!pageColors().dark);
    const QMargins m = android_system::systemBarInsets();
    if (m == m_insets) return;
    m_insets = m;
    emit insetsChanged();
}

QString MobileApp::dictionaryFile(const QString& key) const
{
    for (const DictionaryKind& k : kDictionaries)
        if (key == QLatin1String(k.key)) return QDir(m_folder).filePath(QStringLiteral("dictionaries/") + QLatin1String(k.file));
    return {};
}

core::OfflineDictionary* MobileApp::dictionaryFor(const QString& key) const
{
    if (key == QLatin1String("es")) return m_dictionary;
    if (key == QLatin1String("en")) return m_dictionaryEn;
    if (key == QLatin1String("es-es")) return m_definitionsEs;
    return nullptr;
}

// ---------------------------------------------------------------- library

void MobileApp::importBook(const QUrl& url)
{
    if (!m_library || !url.isValid()) return;
    // A content:// address (Android's file picker) is read through Qt's Android file support; a file:// one directly.
    const QString source = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QString name = safeFileName(QFileInfo(source).fileName());
    QDir dir(QDir(m_folder).filePath(QStringLiteral("books")));
    dir.mkpath(QStringLiteral("."));
    // Copied in first under a temporary name: a picked file often has no name ending (content://…), so the
    // format is read from its first bytes before it gets its name.
    const QString incoming = dir.filePath(QStringLiteral(".incoming"));
    QFile::remove(incoming);
    if (!QFile::copy(source, incoming)) {
        setMessage(tr("The book couldn't be copied into the app."));
        return;
    }
    if (!knownSuffix(name)) {
        QFile f(incoming);
        const QByteArray head = f.open(QIODevice::ReadOnly) ? f.read(4096) : QByteArray();
        f.close();
        const QString suffix = core::convert::bookSuffixFromBytes(head);
        if (suffix.isEmpty() || !core::FormatRegistry::instance().fileSuffixes().contains(suffix)) {
            QFile::remove(incoming);
            setMessage(tr("This file isn't a book FreeReader can open."));
            return;
        }
        if (name.isEmpty() || name.startsWith(QLatin1Char('.'))) name = QStringLiteral("book");
        name += QLatin1Char('.') + suffix;
    }
    // "Book.fb2.zip" keeps both parts of its ending when a number is added.
    QString base = name, ending;
    for (const QString& s : core::FormatRegistry::instance().fileSuffixes())
        if (name.toLower().endsWith(QLatin1Char('.') + s) && s.size() + 1 > ending.size()) {
            ending = name.right(s.size() + 1);
            base = name.left(name.size() - ending.size());
        }
    QString target = dir.filePath(name);
    for (int i = 2; QFileInfo::exists(target); ++i) target = dir.filePath(QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(ending));
    if (!QFile::rename(incoming, target)) {
        QFile::remove(incoming);
        setMessage(tr("The book couldn't be copied into the app."));
        return;
    }
    m_busy = true;
    emit busyChanged();
    setMessage(tr("Adding %1…").arg(QFileInfo(name).completeBaseName()));
    m_importer->add({target});
}

QStringList MobileApp::bookFilters() const
{
#ifdef Q_OS_ANDROID
    // Android's picker turns name filters into file types and greys out the ones it doesn't know (.cbz, .cbr,
    // .azw3…): every file can be picked, and the format is read from its bytes.
    return {tr("All Files (*)")};
#endif
    QStringList patterns;
    for (const QString& s : core::FormatRegistry::instance().fileSuffixes()) patterns << QStringLiteral("*.") + s;
    return {tr("Books (%1)").arg(patterns.join(QLatin1Char(' '))), tr("All Files (*)")};
}

bool MobileApp::rightToLeft() const
{
    const QString key = QString::number(m_bookId) + QLatin1Char('=');
    for (const QString& d : m_settings.pageDirections)
        if (d.startsWith(key)) return d.mid(key.size()) == QLatin1String("rtl");
    const core::Book* book = m_engine ? m_engine->book() : nullptr;
    return book && book->metadata.rightToLeft;
}

void MobileApp::setRightToLeft(bool on)
{
    if (!m_bookId || on == rightToLeft()) return;
    const QString key = QString::number(m_bookId) + QLatin1Char('=');
    for (int i = int(m_settings.pageDirections.size()) - 1; i >= 0; --i)
        if (m_settings.pageDirections[i].startsWith(key)) m_settings.pageDirections.removeAt(i);
    const core::Book* book = m_engine->book();
    if (!book || book->metadata.rightToLeft != on) m_settings.pageDirections << key + (on ? QStringLiteral("rtl") : QStringLiteral("ltr"));
    saveSettings();
    emit directionChanged();
}

void MobileApp::openBook(int id)
{
    if (!m_library) return;
    const core::LibraryBook b = m_library->book(id);
    if (b.id == 0) return;
    if (!QFileInfo::exists(b.path)) {
        setMessage(tr("The book's file is missing."));
        return;
    }
    if (m_bookId) closeBook();
    m_openingPath = b.path;
    m_openingId = id;
    setMessage(QString());
    m_engine->open(b.path);
}

void MobileApp::onOpened()
{
    const core::Book* book = m_engine->book();
    if (!book) return;
    m_bookId = m_library ? m_library->touchBook(m_openingPath, book->metadata) : 0;
    if (!m_bookId) m_bookId = m_openingId;
    m_openingPath.clear();
    m_title = book->metadata.title;
    emit directionChanged();
    m_start = {};
    core::SavedPosition saved;
    if (m_library && m_library->loadPosition(m_bookId, &saved)) {
        if (saved.cfi.isEmpty() || !m_engine->positionForCfi(saved.cfi, &m_start)) m_start = saved.position;
    }
    if (m_start.spine < 0 || m_start.spine >= int(book->spine.size())) m_start = {};
    // Opened for Show in Book (the Word List): at the word, then the word selected once the page is there.
    if (m_pendingWord.bookId == m_bookId && m_pendingWord.spine >= 0 && m_pendingWord.spine < int(book->spine.size())) {
        core::Position p{m_pendingWord.spine, m_pendingWord.start};
        if (!m_pendingWord.cfi.isEmpty()) m_engine->positionForCfi(m_pendingWord.cfi, &p);
        m_start = p;
        const int length = m_pendingWord.end - m_pendingWord.start;
        QTimer::singleShot(600, this, [this, p, length] { emit selectRequested(p.spine, p.offset, p.offset + length); });
    }
    m_pendingWord = {};
    m_position = m_start;
    m_screenStart = m_screenEnd = {-1, 0};
    forgetHistory();
    m_footnoteTarget = {-1, 0};
    m_model->refresh();
    emit readingChanged();
    emit locationChanged();
    emit annotationsChanged();
    emit contentsChanged();
    emit optionsChanged();   // Keep This Book Offline is the open book's
    emit lookChanged();      // the book's and its language's own text settings
    emit bookOpened();
    startSpeechEngine();     // ready by the time Listen or Hear Pronunciation is tapped
}

void MobileApp::closeBook()
{
    if (!m_bookId) return;
    savePosition();
    m_engine->close();
    m_bookId = 0;
    m_title.clear();
    m_chapter.clear();
    m_pageText.clear();
    forgetHistory();
    closeImage();
    closeLookup();
    closeCard();
    m_engine->cancelSearch();
    m_searchId = 0;
    m_hits.clear();
    m_searchSummary.clear();
    emit searchChanged();
    emit annotationsChanged();
    emit contentsChanged();
    emit optionsChanged();
    emit lookChanged();
    m_model->refresh();
    emit readingChanged();
    emit locationChanged();
}

void MobileApp::removeBook(int id)
{
    if (!m_library) return;
    const core::LibraryBook b = m_library->book(id);
    if (b.id == 0) return;
    if (id == m_bookId) closeBook();
    m_library->removeBook(id);
    // Only the app's own copy (in its books folder) is deleted.
    const QString own = QDir(QDir(m_folder).filePath(QStringLiteral("books"))).canonicalPath();
    const QString file = QFileInfo(b.path).canonicalFilePath();
    if (!own.isEmpty() && file.startsWith(own + QLatin1Char('/'))) QFile::remove(file);
    m_model->refresh();
    emit booksChanged();
}

QString MobileApp::languageCode() const
{
    const core::Book* book = m_engine->book();
    if (!book || book->metadata.language.isEmpty()) return {};
    return book->metadata.language.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
}

QString MobileApp::chapterTitleAt(const core::Position& p) const
{
    const auto ix = m_engine->index();
    const core::Book* book = m_engine->book();
    if (!ix || !book) return {};
    const int e = ix->tocEntryAt(p);
    return e >= 0 ? book->toc[size_t(e)].title : QString();
}

void MobileApp::pageMoved(QObject* object)
{
    auto* page = qobject_cast<BookPage*>(object);
    if (!page || !m_bookId) return;
    // Learn the reading speed from forward page turns at a plausible pace (as the desktop does).
    const core::Position before = m_position;
    if (const auto ix = m_engine->index(); ix && before < page->position() && m_sinceTurn.isValid()) {
        const qint64 chars = ix->global(page->position()) - ix->global(before);
        const double seconds = m_sinceTurn.elapsed() / 1000.0;
        if (chars > 200 && chars < 6000 && seconds > 3 && seconds < 600) {
            const double cpm = chars / (seconds / 60.0);
            m_settings.readingSpeed = qBound(200, int(0.85 * m_settings.readingSpeed + 0.15 * cpm), 6000);
        }
    }
    m_sinceTurn.restart();
    m_position = page->position();
    m_screenStart = page->screenStart();
    m_screenEnd = page->screenEnd();
    if (m_settings.showBilingual) m_alongsideTimer.start();
    updateLocation();
    m_saveTimer.start();
    emit bookmarkChanged();
}

void MobileApp::updateLocation()
{
    if (!m_bookId) return;
    m_chapter = chapterTitleAt(m_position);
    if (const auto ix = m_engine->index()) {
        m_progress = ix->fraction(m_position);
        m_pageText = tr("Page %1 of %2").arg(ix->pageLabel(m_position), ix->lastPageLabel());
        m_chapterLeft = core::chapterLeftText(*ix, m_position, m_settings.readingSpeed, m_settings.footerPagesLeft);
    }
    emit locationChanged();
}

void MobileApp::toggleChapterLeft()
{
    m_settings.footerPagesLeft = !m_settings.footerPagesLeft;
    saveSettings();
    updateLocation();
}

void MobileApp::savePosition()
{
    if (!m_bookId || !m_library || !m_engine->isOpen()) return;
    core::SavedPosition s;
    s.position = m_position;
    s.cfi = m_engine->cfiFor(m_position);
    if (const auto ix = m_engine->index()) {
        s.progress = ix->fraction(m_position);
        s.pageLabel = ix->pageLabel(m_position);
        s.pageCount = ix->lastPageLabel();
    }
    m_library->savePosition(m_bookId, s);
}

bool MobileApp::waitUntilOpen(int ms)
{
    QElapsedTimer t;
    t.start();
    while (!m_bookId && t.elapsed() < ms && (!m_openingPath.isEmpty())) {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return m_bookId > 0;
}

// ---------------------------------------------------------------- lookups

void MobileApp::lookUp(QObject* object)
{
    auto* page = qobject_cast<BookPage*>(object);
    if (!page || !page->hasSelection()) return;
    QString text = page->selectedText();
    if (text.size() > 2000) text = text.left(2000);
    core::TranslationRequest r;
    r.text = text;
    r.sentence = page->selectionSentence();
    if (r.sentence.size() > 600 || r.sentence == r.text) r.sentence.clear();
    r.from = languageCode();
    m_asked = {page->selectionSpine(), page->selectionStart(), page->selectionEnd(), r.sentence.isEmpty() ? r.text : r.sentence};
    m_lookup = QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("loading"), true}, {QStringLiteral("isWord"), r.isWord()}};
    emit lookupChanged();
    m_translator->setOfflineOnly(bookOffline());   // Keep This Book Offline: Gemini isn't asked about it
    m_lookupId = m_translator->translate(r);
}

void MobileApp::explain(QObject* object)
{
    // Explain This Sentence (as on the desktop): the selection's sentence translated, with notes on its grammar and
    // idioms; a selection longer than a sentence is explained as it is. Only an AI can answer it.
    auto* page = qobject_cast<BookPage*>(object);
    if (!page || !page->hasSelection()) return;
    QString text = page->selectionSentence().simplified();
    const QString selected = page->selectedText().simplified();
    if (selected.size() > text.size()) text = selected;
    if (text.isEmpty()) return;
    if (text.size() > 1500) text = text.left(1500);
    core::TranslationRequest r;
    r.kind = core::TranslationRequest::Kind::Explain;
    r.text = text;
    r.sentence = text;
    r.from = languageCode();
    m_asked = {page->selectionSpine(), page->selectionStart(), page->selectionEnd(), text};
    m_lookup = QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("loading"), true}, {QStringLiteral("isWord"), false},
                           {QStringLiteral("explain"), true}};
    emit lookupChanged();
    m_translator->setOfflineOnly(bookOffline());
    m_lookupId = m_translator->translate(r);
}

void MobileApp::closeLookup()
{
    m_lookupId = -1;
    m_lookup.clear();
    emit lookupChanged();
}

void MobileApp::onTranslated(int id, const core::TranslationResult& r, const QString& why)
{
    if (m_alongsideAsked.contains(id)) {
        onAlongsideTranslated(id, r, why);
        return;
    }
    if (id != m_lookupId) return;
    QVariantMap m;
    m[QStringLiteral("text")] = m_lookup.value(QStringLiteral("text"));
    m[QStringLiteral("isWord")] = m_lookup.value(QStringLiteral("isWord"));
    m[QStringLiteral("loading")] = false;
    const bool explaining = m_lookup.value(QStringLiteral("explain")).toBool();
    m[QStringLiteral("explain")] = explaining;
    if (r.isEmpty()) {
        QString text = why.isEmpty() ? tr("No translation engine is set up yet.") : why;
        if (explaining)
            text = bookOffline() ? tr("Explanations come from Gemini, and this book is kept offline (Settings).")
                 : !m_gemini->hasKey() || !m_settings.useGemini ? tr("Explanations come from Gemini: add a key, or turn Gemini on, in Settings.")
                 : tr("Explanations come from Gemini, and it couldn't answer.") + (why.isEmpty() ? QString() : QLatin1Char('\n') + why);
        else if (bookOffline() && m_settings.useGemini)
            text += QLatin1Char('\n') + tr("This book is kept offline, so Gemini isn't asked.");
        else if (!m_dictionary->info().ok) text += QLatin1Char('\n') + tr("Tip: add a Gemini key, or download the offline dictionary, in Settings.");
        if (!explaining && m_settings.useGemini && m_settings.geminiExplainOnly)
            text += QLatin1Char('\n') + tr("Gemini is used only for Explain This Sentence (Settings).");
        m[QStringLiteral("nothing")] = text;
    } else if (explaining) {
        // An explanation isn't vocabulary: shown, not logged.
        m[QStringLiteral("translation")] = r.translation;
        m[QStringLiteral("explanation")] = r.explanation;
        m[QStringLiteral("source")] = r.provider.isEmpty() ? QString() : tr("Source: %1").arg(r.provider);
        m[QStringLiteral("provider")] = r.provider;
        m[QStringLiteral("ai")] = r.provider == m_gemini->name();
    } else {
        m[QStringLiteral("translation")] = r.translation;
        m[QStringLiteral("alternatives")] = r.alternatives.join(QStringLiteral(", "));
        m[QStringLiteral("partOfSpeech")] = r.partOfSpeech;
        m[QStringLiteral("lemma")] = r.lemma;
        m[QStringLiteral("definition")] = r.definition;
        m[QStringLiteral("inSentence")] = r.inSentence;
        m[QStringLiteral("note")] = r.note;
        m[QStringLiteral("source")] = r.provider.isEmpty() ? QString() : tr("Source: %1").arg(r.provider);
        m[QStringLiteral("provider")] = r.provider;
        m[QStringLiteral("ai")] = r.provider == m_gemini->name();   // an AI's answer: it can be reported
        // A lookup joins the word log, with the place it was seen, only when chosen (the desktop's Practice > Add
        // Looked-Up Words): by default the Word List is what was highlighted (the owner's rule, 2026-11-26; the phone
        // app still logged every lookup until 2026-11-27).
        if (m_words && m_bookId && m_settings.practiceLookups) {
            core::WordPlace place;
            place.bookId = m_bookId;
            place.bookTitle = m_title;
            place.language = languageCode();
            place.spine = m_asked.spine;
            place.start = m_asked.start;
            place.end = m_asked.end;
            place.sentence = m_asked.sentence;
            const core::Position at{m_asked.spine, m_asked.start};
            place.cfi = m_engine->cfiFor(at);
            if (const auto ix = m_engine->index()) place.pageLabel = ix->pageLabel(at);
            place.chapter = chapterTitleAt(at);
            m_words->record(r, place);
            emit wordsChanged();
        }
    }
    m_lookup = m;
    emit lookupChanged();
}

// ---------------------------------------------------------------- Word List and practice

QVariantList MobileApp::words() const
{
    // The Word List as the desktop's: searched (word, dictionary form, meaning, other meanings; accents ignored), one
    // book or all, in the order chosen (kept in the settings, the same choices as the desktop's Sort).
    QVariantList out;
    if (!m_words) return out;
    const QString needle = core::practice::foldAccents(m_wordSearch.trimmed().toLower());
    QList<core::WordEntry> list;
    for (const core::WordEntry& w : m_words->words(m_wordBook)) {
        if (!needle.isEmpty()) {
            const QString hay = core::practice::foldAccents((w.text + QLatin1Char(' ') + w.lemma + QLatin1Char(' ') + w.translation +
                                                             QLatin1Char(' ') + w.alternatives.join(QLatin1Char(' '))).toLower());
            if (!hay.contains(needle)) continue;
        }
        list << w;
    }
    const QString key = m_settings.wordListSort;
    using W = core::WordEntry;
    auto folded = [](const QString& s) { return core::practice::foldAccents(s.toLower()); };
    auto alpha = [&](const QString& a, const QString& b) { return QString::localeAwareCompare(folded(a), folded(b)); };
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
    if (base == QLatin1String("review")) {
        // The soonest review first (latest, the other way); words not practiced yet after them either way.
        const QHash<int, QDateTime> due = m_words->nextReviews();
        const bool latest = key == QLatin1String("review-latest");
        std::stable_sort(list.begin(), list.end(), [&](const W& a, const W& b) {
            const QDateTime da = due.value(a.id), db = due.value(b.id);
            if (da.isValid() != db.isValid()) return da.isValid();
            return da.isValid() && (latest ? db < da : da < db);
        });
    } else if (before) {
        const bool reversed = key == QLatin1String("za") || key.contains(QLatin1Char('-'));
        if (reversed) std::stable_sort(list.begin(), list.end(), [&](const W& a, const W& b) { return before(b, a); });
        else std::stable_sort(list.begin(), list.end(), before);
    }
    for (const core::WordEntry& w : std::as_const(list)) {
        out << QVariantMap{{QStringLiteral("id"), w.id},
                           {QStringLiteral("headword"), w.headword()},
                           {QStringLiteral("text"), w.text},
                           {QStringLiteral("translation"), w.translation},
                           {QStringLiteral("partOfSpeech"), w.partOfSpeech},
                           {QStringLiteral("sentence"), w.sentence},
                           {QStringLiteral("book"), w.bookTitle},
                           {QStringLiteral("page"), w.pageLabel},
                           {QStringLiteral("starred"), w.starred},
                           {QStringLiteral("lookups"), w.lookups},
                           {QStringLiteral("needsMeaning"), w.translation.isEmpty()}};
    }
    return out;
}

void MobileApp::startOverWord(int id)
{
    // Start Over (as on the desktop): the word's cards become new again.
    if (!m_words || id <= 0) return;
    m_words->resetCards(id);
    emit wordsChanged();
    refreshPractice();
}

void MobileApp::showWordInBook(int id)
{
    // Show in Book: the place the word was seen, selected; its book opened first when another one is open.
    if (!m_words) return;
    const core::WordEntry w = m_words->word(id);
    if (w.id == 0 || !w.bookId || w.spine < 0) return;
    core::Position p{w.spine, w.start};
    if (w.bookId == m_bookId) {
        if (!w.cfi.isEmpty()) m_engine->positionForCfi(w.cfi, &p);
        jumpTo(p);
        emit selectRequested(p.spine, p.offset, p.offset + (w.end - w.start));
        return;
    }
    m_pendingWord = {w.bookId, w.spine, w.start, w.end, w.cfi};
    openBook(w.bookId);
}

int MobileApp::wordCount() const { return m_words ? m_words->count() : 0; }

void MobileApp::removeWord(int id)
{
    if (!m_words) return;
    m_words->remove(id);
    emit wordsChanged();
    refreshPractice();
}

void MobileApp::setWordStarred(int id, bool on)
{
    if (!m_words) return;
    core::WordEntry w = m_words->word(id);
    if (w.id == 0) return;
    w.starred = on;
    m_words->update(w);
    emit wordsChanged();
}

void MobileApp::setWordMeaning(int id, const QString& meaning)
{
    if (!m_words) return;
    core::WordEntry w = m_words->word(id);
    if (w.id == 0) return;
    w.translation = meaning.trimmed();
    m_words->update(w);
    emit wordsChanged();
    refreshPractice();
}

namespace {

// The cards to practice: the directions and passages of the settings, and this session's book and starred choice
// (as the desktop's "Words From" and "Starred Words Only").
core::WordLog::Filter practiceFilter(const Settings& s, int bookId = 0, bool starredOnly = false)
{
    core::WordLog::Filter f;
    f.directions = s.practiceDirections.isEmpty() ? QStringList{QStringLiteral("recognize")} : s.practiceDirections;
    f.passages = s.practicePassages;
    f.bookId = bookId;
    f.starredOnly = starredOnly;
    return f;
}

QString directionName(const QString& d, const QString& language)
{
    const QString lang = language.isEmpty() || language == QLatin1String("und") ? QObject::tr("Book Language")
                                                                                 : QLocale::languageToString(QLocale(language).language());
    if (d == QLatin1String("recall")) return QObject::tr("English → %1").arg(lang);
    if (d == QLatin1String("definition")) return QObject::tr("Definition → Word");
    if (d == QLatin1String("cloze")) return QObject::tr("Fill the Gap");
    return QObject::tr("%1 → English").arg(lang);
}

}  // namespace

QVariantMap MobileApp::practice() const
{
    QVariantMap m;
    if (!m_words) return m;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    // Every card is practiced, in the order chosen (no "due" or "new today", as on the desktop).
    m[QStringLiteral("cards")] = m_words->practisableCount(practiceFilter(m_settings, m_practiceBook, m_practiceStarred));
    m[QStringLiteral("book")] = m_practiceBook;
    m[QStringLiteral("starredOnly")] = m_practiceStarred;
    m[QStringLiteral("typed")] = m_settings.practiceTyped;
    m[QStringLiteral("words")] = m_words->count();
    m[QStringLiteral("summary")] = m_practiceSummary;
    const bool active = m_session && m_session->isActive();
    m[QStringLiteral("active")] = active;
    if (!active) return m;
    const core::WordEntry& w = m_session->word();
    const QString d = m_session->card().direction;
    const core::practice::CardFace f = core::practice::cardFace(w, d, m_settings.practiceSentenceFront, false, pageColors().muted);
    m[QStringLiteral("direction")] = directionName(d, w.language);
    m[QStringLiteral("front")] = f.front;
    m[QStringLiteral("frontDetail")] = f.frontDetail;
    m[QStringLiteral("frontSentence")] = f.frontSentenceHtml;
    m[QStringLiteral("frontIsLong")] = f.frontIsLong;
    m[QStringLiteral("back")] = f.back;
    m[QStringLiteral("backDetail")] = f.backDetail.join(QLatin1Char('\n'));
    m[QStringLiteral("backSentence")] = f.backSentenceHtml;
    m[QStringLiteral("source")] = f.source;
    m[QStringLiteral("needsMeaning")] = f.needsMeaning;
    m[QStringLiteral("revealed")] = m_revealed;
    m[QStringLiteral("verdict")] = m_verdict;       // Type the Answer: how the typed answer went
    m[QStringLiteral("suggested")] = m_suggested;   // the rating that verdict suggests (1 Again .. 4 Easy; 0 none)
    m[QStringLiteral("progress")] = tr("%1 of %2").arg(m_session->done() + 1).arg(m_session->done() + m_session->remaining() + 1);
    Q_UNUSED(now);
    return m;
}

void MobileApp::refreshPractice() { emit practiceChanged(); }

void MobileApp::setPracticeOrder(const QString& order)
{
    if (order == m_settings.practiceOrder || !core::WordLog::orders().contains(order)) return;
    m_settings.practiceOrder = order;
    saveSettings();
    emit practiceChanged();
}

bool MobileApp::startPractice()
{
    if (!m_words) return false;
    if (!m_session) m_session = std::make_unique<core::practice::Session>(m_words.get());
    m_practiceSummary.clear();
    m_revealed = false;
    m_verdict.clear();
    m_suggested = 0;
    const bool ok = m_session->start(practiceFilter(m_settings, m_practiceBook, m_practiceStarred), m_settings.practiceOrder, QDateTime::currentDateTimeUtc());
    if (!ok) m_practiceSummary = tr("Nothing to practice now. Highlight words while reading, then come back.");
    emit practiceChanged();
    return ok;
}

void MobileApp::revealCard()
{
    if (!m_session || !m_session->isActive()) return;
    m_revealed = true;
    emit practiceChanged();
}

void MobileApp::checkTyped(const QString& typed)
{
    // Type the Answer (as on the desktop): forgiving about case, articles and punctuation; a missing accent or a
    // small typo is told apart from a wrong answer, and suggests a rating.
    if (!m_session || !m_session->isActive() || m_revealed) return;
    using core::practice::AnswerMatch;
    const core::WordEntry& w = m_session->word();
    const QString d = m_session->card().direction;
    const core::practice::AnswerCheck c = core::practice::checkAnswer(typed, core::practice::acceptedAnswers(w, d), core::practice::answerLanguage(w, d));
    switch (c.match) {
    case AnswerMatch::Correct: m_verdict = tr("Correct."); m_suggested = 3; break;
    case AnswerMatch::Accents: m_verdict = tr("Right, but mind the accents: %1").arg(c.expected); m_suggested = 2; break;
    case AnswerMatch::Typo: m_verdict = tr("Almost: %1").arg(c.expected); m_suggested = 2; break;
    case AnswerMatch::Wrong:
        m_verdict = typed.trimmed().isEmpty() ? tr("The answer: %1").arg(c.expected) : tr("Not quite. The answer: %1").arg(c.expected);
        m_suggested = 1;
        break;
    }
    m_revealed = true;
    emit practiceChanged();
}

QVariantList MobileApp::wordBooks() const
{
    // The books the Word List has words from: the practice's "Words From" choice.
    QVariantList out;
    if (!m_words) return out;
    QHash<int, QString> seen;
    for (const core::WordEntry& w : m_words->words())
        if (w.bookId && !seen.contains(w.bookId)) seen.insert(w.bookId, w.bookTitle);
    for (auto it = seen.cbegin(); it != seen.cend(); ++it) out << QVariantMap{{QStringLiteral("id"), it.key()}, {QStringLiteral("title"), it.value()}};
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return QString::localeAwareCompare(a.toMap().value(QStringLiteral("title")).toString(), b.toMap().value(QStringLiteral("title")).toString()) < 0;
    });
    return out;
}

void MobileApp::rateCard(int rating)
{
    if (!m_session || !m_session->isActive() || !m_revealed || rating < 1 || rating > 4) return;
    m_session->rate(core::practice::Rating(rating), QDateTime::currentDateTimeUtc());
    m_revealed = false;
    m_verdict.clear();
    m_suggested = 0;
    if (!m_session->isActive()) endPractice();
    else emit practiceChanged();
}

void MobileApp::endPractice()
{
    if (!m_session) return;
    using core::practice::Rating;
    const int done = m_session->done();
    m_practiceSummary = done == 0 ? tr("No cards answered.")
                                  : tr("Done for now: %1.").arg(countText(done, tr("card"), tr("cards"))) + QLatin1Char('\n') +
                                        tr("Again %1 · Hard %2 · Good %3 · Easy %4")
                                            .arg(m_session->ratings(Rating::Again))
                                            .arg(m_session->ratings(Rating::Hard))
                                            .arg(m_session->ratings(Rating::Good))
                                            .arg(m_session->ratings(Rating::Easy));
    m_session->clear();
    m_revealed = false;
    emit practiceChanged();
}

// ---------------------------------------------------------------- settings

void MobileApp::saveSettings() { m_settings.save(QDir(m_folder).filePath(QStringLiteral("settings.json"))); }

// ---------------------------------------------------------------- speech

void MobileApp::speak(const QString& text, const QString& language)
{
    // Hear Pronunciation and Read Aloud: the phone's own voices, at the speed and volume of the settings (as on the
    // desktop). Tests and the screenshot mode never start the system's speech engine.
    const QString said = text.simplified();
    if (said.isEmpty()) return;
    m_lastSpoken = said;
    if (m_quiet) return;
#ifdef HAVE_TEXT_TO_SPEECH
    startSpeechEngine();
    if (m_speech->state() != QTextToSpeech::Ready && m_speech->state() != QTextToSpeech::Speaking) {
        // Android starts its speech engine in the background: the words are said once it is ready (a few seconds
        // at most), else it says what is wrong (the emulator, 2026-11-27: the first tap came before "TTS initialized").
        m_pendingSpeech = {said, language};
        QTimer::singleShot(5000, this, [this] {
            if (m_pendingSpeech.first.isEmpty()) return;
            m_pendingSpeech = {};
            setMessage(tr("This phone's speech engine isn't working. Check Settings › General management › Text-to-speech."));
        });
        return;
    }
    sayNow(said, language);
#else
    Q_UNUSED(language);
    setMessage(tr("Reading aloud isn't available in this build."));
#endif
}

void MobileApp::startSpeechEngine()
{
#ifdef HAVE_TEXT_TO_SPEECH
    if (m_speech || m_quiet) return;
    m_speech = new QTextToSpeech(this);
    connect(m_speech, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State s) {
        emit speakingChanged();
        if (s == QTextToSpeech::Ready && !m_pendingSpeech.first.isEmpty()) {
            const auto p = m_pendingSpeech;
            m_pendingSpeech = {};
            sayNow(p.first, p.second);
        }
    });
#endif
}

void MobileApp::sayNow(const QString& said, const QString& language)
{
#ifdef HAVE_TEXT_TO_SPEECH
    const QLocale want(language.isEmpty() ? QStringLiteral("en") : language);
    QLocale best;
    for (const QLocale& l : m_speech->availableLocales())
        if (l.language() == want.language() && (best.language() != want.language() || l.territory() == want.territory())) best = l;
    if (best.language() != want.language()) {
        // Another language's voice would read the word wrongly: nothing is said, and the fix is a free voice.
        setMessage(tr("No %1 voice is installed. Add one (free) in the phone's Settings › General management › "
                      "Text-to-speech.").arg(QLocale::languageToString(want.language())));
        return;
    }
    m_speech->setLocale(best);
    m_speech->setRate(m_settings.speechRatePct / 100.0);
    m_speech->setVolume(m_settings.speechVolumePct / 100.0);
    m_speech->say(said);
#else
    Q_UNUSED(language);
    setMessage(tr("Reading aloud isn't available in this build."));
#endif
}

void MobileApp::hear(const QString& text) { speak(text, languageCode()); }

void MobileApp::readAloud(QObject* object)
{
    auto* page = qobject_cast<BookPage*>(object);
    if (!page || !page->hasSelection()) return;
    speak(page->selectedText(), languageCode());
}

void MobileApp::stopSpeaking()
{
#ifdef HAVE_TEXT_TO_SPEECH
    if (m_speech) m_speech->stop();
#endif
}

bool MobileApp::isSpeaking() const
{
#ifdef HAVE_TEXT_TO_SPEECH
    return m_speech && m_speech->state() == QTextToSpeech::Speaking;
#else
    return false;
#endif
}

bool MobileApp::bookOffline() const { return m_bookId && m_settings.offlineBooks.contains(QString::number(m_bookId)); }

QVariantMap MobileApp::options() const
{
    // The settings the screens show and change (the same ones, with the same meaning, as the desktop's).
    const Settings& s = m_settings;
    return {{QStringLiteral("useGemini"), s.useGemini},
            {QStringLiteral("geminiExplainOnly"), s.geminiExplainOnly},
            {QStringLiteral("geminiModel"), s.geminiModel.isEmpty() ? QString::fromLatin1(core::GeminiProvider::kDefaultModel) : s.geminiModel},
            {QStringLiteral("geminiWaitSec"), s.geminiWaitMs / 1000.0},
            {QStringLiteral("useDictionary"), s.useDictionary},
            {QStringLiteral("bookOffline"), bookOffline()},
            {QStringLiteral("practiceHighlights"), s.practiceHighlights},
            {QStringLiteral("practiceLookups"), s.practiceLookups},
            {QStringLiteral("practiceTyped"), s.practiceTyped},
            {QStringLiteral("practiceSentenceFront"), s.practiceSentenceFront},
            {QStringLiteral("practicePassages"), s.practicePassages},
            {QStringLiteral("practiceDirections"), s.practiceDirections},
            {QStringLiteral("practiceBook"), m_practiceBook},
            {QStringLiteral("practiceStarredOnly"), m_practiceStarred},
            {QStringLiteral("wordSearch"), m_wordSearch},
            {QStringLiteral("wordBook"), m_wordBook},
            {QStringLiteral("wordListSort"), s.wordListSort}};
}

void MobileApp::setOption(const QString& key, const QVariant& value)
{
    Settings& s = m_settings;
    if (key == QLatin1String("useGemini")) s.useGemini = value.toBool();
    else if (key == QLatin1String("geminiExplainOnly")) s.geminiExplainOnly = value.toBool();
    else if (key == QLatin1String("geminiModel")) {
        const QString v = value.toString().trimmed();
        s.geminiModel = v == QLatin1String(core::GeminiProvider::kDefaultModel) ? QString() : v;
    } else if (key == QLatin1String("geminiWaitSec")) s.geminiWaitMs = qBound(0, int(value.toDouble() * 1000 + 0.5), 30000);
    else if (key == QLatin1String("useDictionary")) s.useDictionary = value.toBool();
    else if (key == QLatin1String("bookOffline")) {
        if (!m_bookId) return;
        const QString id = QString::number(m_bookId);
        s.offlineBooks.removeAll(id);
        if (value.toBool()) s.offlineBooks << id;
    } else if (key == QLatin1String("wordSearch") || key == QLatin1String("wordBook")) {
        // The Word List's search and book: this session's only.
        if (key == QLatin1String("wordSearch")) m_wordSearch = value.toString();
        else m_wordBook = value.toInt();
        emit optionsChanged();
        emit wordsChanged();
        return;
    } else if (key == QLatin1String("wordListSort")) {
        static const QStringList keys{"recent", "newest", "oldest", "az", "za", "meaning", "meaning-za", "book", "book-reverse",
                                      "lookups", "lookups-least", "starred", "review", "review-latest"};
        if (!keys.contains(value.toString())) return;
        s.wordListSort = value.toString();
        saveSettings();
        emit optionsChanged();
        emit wordsChanged();
        return;
    } else if (key == QLatin1String("practiceBook") || key == QLatin1String("practiceStarredOnly")) {
        // This session's choice only, as on the desktop (not kept in the settings file).
        if (key == QLatin1String("practiceBook")) m_practiceBook = value.toInt();
        else m_practiceStarred = value.toBool();
        emit optionsChanged();
        emit practiceChanged();
        return;
    } else if (key == QLatin1String("practiceHighlights")) s.practiceHighlights = value.toBool();
    else if (key == QLatin1String("practiceLookups")) s.practiceLookups = value.toBool();
    else if (key == QLatin1String("practiceTyped")) s.practiceTyped = value.toBool();
    else if (key == QLatin1String("practiceSentenceFront")) s.practiceSentenceFront = value.toBool();
    else if (key == QLatin1String("practicePassages")) s.practicePassages = value.toBool();
    else if (key == QLatin1String("practiceDirections")) {
        QStringList d = value.toStringList();
        d.removeAll(QString());
        s.practiceDirections = d.isEmpty() ? QStringList{QStringLiteral("recognize")} : d;
    } else return;
    saveSettings();
    applyTranslationSettings();
    emit optionsChanged();
    emit practiceChanged();
}

void MobileApp::applyTranslationSettings()
{
    m_gemini->setEnabled(m_settings.useGemini);
    m_gemini->setExplainOnly(m_settings.geminiExplainOnly);
    m_gemini->setModel(m_settings.geminiModel);
    m_translator->setWait(m_gemini->name(), qMax(0, m_settings.geminiWaitMs));
    for (core::OfflineDictionary* d : {m_dictionary, m_dictionaryEn, m_definitionsEs}) d->setEnabled(m_settings.useDictionary);
    m_translator->clearPauses();
}

void MobileApp::testGemini()
{
    // One word looked up with the key saved (uses one of today's free lookups), as the desktop's Test.
    if (!m_gemini->hasKey()) {
        emit geminiTested(tr("Save a key first."));
        return;
    }
    core::TranslationRequest r;
    r.text = QStringLiteral("casa");
    r.from = QStringLiteral("es");
    QElapsedTimer* t = new QElapsedTimer;
    t->start();
    QPointer<MobileApp> self(this);
    const bool wasEnabled = m_settings.useGemini;
    m_gemini->setEnabled(true);
    m_gemini->translate(r, [self, t, wasEnabled](const core::TranslationResult& res, core::TranslationFailure f, const QString&) {
        const qint64 ms = t->elapsed();
        delete t;
        if (!self) return;
        self->m_gemini->setEnabled(wasEnabled && self->m_settings.useGemini);
        if (f == core::TranslationFailure::None && !res.translation.isEmpty())
            emit self->geminiTested(tr("Working: “casa” is “%1” (%2 s).").arg(res.translation, QLocale().toString(ms / 1000.0, 'f', 1)));
        else
            emit self->geminiTested(core::failureText(f == core::TranslationFailure::None ? core::TranslationFailure::Failed : f));
    });
}

void MobileApp::setTextSize(int pct)
{
    pct = qBound(60, pct, 250);
    if (pct == textSize()) return;
    setLook(QStringLiteral("text_size_pct"), pct);
}

void MobileApp::setMargins(int px)
{
    px = qBound(8, px, 80);
    if (px == margins()) return;
    setLook(QStringLiteral("margin_px"), px);
}

// ---------------------------------------------------------------- text and layout

namespace {
QString langScopeOf(const QString& code) { return code.isEmpty() ? QString() : QStringLiteral("lang:") + code; }
QString bookScopeOf(int id) { return id > 0 ? QStringLiteral("book:%1").arg(id) : QString(); }
}  // namespace

Appearance MobileApp::appearance() const
{
    Appearance a = m_settings.look;
    const core::Book* book = m_engine->book();
    if (!m_library || !m_bookId || !book) return a;
    if (book->metadata.pictures) {
        // Comics and scanned pages: the picture as large as the screen allows, one page at a time (as on the
        // desktop).
        a.marginPx = 8;
        a.pageWidthEm = 80;
        a.columns = QStringLiteral("one");
    } else if (const QString lang = langScopeOf(languageCode()); !lang.isEmpty()) {
        a.merge(m_library->appearanceLayer(lang));
    }
    a.merge(m_library->appearanceLayer(bookScopeOf(m_bookId)));
    return a;
}

QVariantMap MobileApp::look() const
{
    const Appearance a = appearance();
    QVariantMap m = a.toJson().toVariantMap();
    const QString lang = m_bookId ? langScopeOf(languageCode()) : QString();
    QString scope = m_lookScope;
    if ((scope == QLatin1String("language") && lang.isEmpty()) || (scope == QLatin1String("book") && !m_bookId)) scope = QStringLiteral("all");
    m.insert(QStringLiteral("scope"), scope);
    const QString code = m_bookId ? languageCode() : QString();
    m.insert(QStringLiteral("languageName"), code.isEmpty() || QLocale(code).language() == QLocale::C ? QString() : core::languages::displayName(code));
    m.insert(QStringLiteral("languageOwn"), m_library && !lang.isEmpty() && !m_library->appearanceLayer(lang).isEmpty());
    m.insert(QStringLiteral("bookOwn"), m_library && m_bookId && !m_library->appearanceLayer(bookScopeOf(m_bookId)).isEmpty());
    m.insert(QStringLiteral("pageTurn"), m_settings.pageTurn);
    m.insert(QStringLiteral("pageTurnMs"), m_settings.pageTurnMs);
    m.insert(QStringLiteral("pageTurnMotion"), m_settings.pageTurnMotion);
    return m;
}

void MobileApp::setLookScope(const QString& scope)
{
    if (scope != QLatin1String("all") && scope != QLatin1String("language") && scope != QLatin1String("book")) return;
    if (m_lookScope == scope) return;
    m_lookScope = scope;
    emit lookChanged();
}

void MobileApp::setLook(const QString& key, const QVariant& value)
{
    if (!Appearance::keys().contains(key)) return;
    const QJsonValue v = QJsonValue::fromVariant(value);
    const QString lang = m_bookId ? langScopeOf(languageCode()) : QString();
    const QString book = bookScopeOf(m_bookId);
    auto setIn = [&](const QString& scope, bool set) {
        if (scope.isEmpty() || !m_library) return;
        QJsonObject layer = m_library->appearanceLayer(scope);
        if (set) layer[key] = v;
        else layer.remove(key);
        m_library->setAppearanceLayer(scope, layer);
    };
    const QString scope = look().value(QStringLiteral("scope")).toString();
    if (scope == QLatin1String("book")) {
        setIn(book, true);
    } else if (scope == QLatin1String("language")) {
        setIn(lang, true);
        setIn(book, false);
    } else {
        // All Books, and taken out of this book's and this language's own settings so it shows (as on the desktop).
        QJsonObject o = m_settings.look.toJson();
        o[key] = v;
        m_settings.look.merge(o);
        setIn(lang, false);
        setIn(book, false);
        saveSettings();
    }
    emit lookChanged();
}

void MobileApp::resetLook(const QString& scope)
{
    if (!m_library || !m_bookId) return;
    if (scope == QLatin1String("book")) m_library->setAppearanceLayer(bookScopeOf(m_bookId), {});
    else if (scope == QLatin1String("language") && !languageCode().isEmpty()) m_library->setAppearanceLayer(langScopeOf(languageCode()), {});
    else return;
    emit lookChanged();
}

void MobileApp::setPageTurn(const QString& style, int ms, const QString& motion)
{
    if (!layout::turnStyles().contains(style) || !layout::turnMotions().contains(motion)) return;
    m_settings.pageTurn = style;
    m_settings.pageTurnMs = qBound(100, ms, 1000);
    m_settings.pageTurnMotion = motion;
    saveSettings();
    emit lookChanged();
}

QVariantList MobileApp::fonts() const
{
    QVariantList out{QVariantMap{{QStringLiteral("key"), QString()}, {QStringLiteral("name"), tr("Book's Fonts")}}};
    QStringList names = Appearance::bundledFonts();
    // The phone's own fonts, but not the dozens of one-script Noto fonts (Noto Sans Bengali …) nor symbol fonts.
    static const QRegularExpression oneScript(QStringLiteral("^Noto (Sans|Serif|Naskh|Kufi|Nastaliq|Rashi)( \\w+)+$|Emoji|Symbol|Icons|Dings|Marlett|MT Extra"));
    QStringList system;
    for (const QString& f : QFontDatabase::families())
        if (!f.startsWith(QLatin1Char('@')) && !QFontDatabase::isPrivateFamily(f) && !names.contains(f) && !oneScript.match(f).hasMatch()) system << f;
    system.sort(Qt::CaseInsensitive);
    for (const QString& f : names + system) out << QVariantMap{{QStringLiteral("key"), f}, {QStringLiteral("name"), f}};
    // A font chosen before that this phone doesn't have any more: still listed, so the choice shows.
    const QString chosen = appearance().font;
    if (!chosen.isEmpty() && !names.contains(chosen) && !system.contains(chosen))
        out << QVariantMap{{QStringLiteral("key"), chosen}, {QStringLiteral("name"), chosen}};
    return out;
}

QVariantList MobileApp::pageTurnStyles() const
{
    // The desktop's styles and their descriptions.
    const QList<std::array<QString, 3>> styles{
        {QStringLiteral("none"), tr("None"), tr("The new page at once.")},
        {QStringLiteral("slide"), tr("Slide"), tr("The new page pushes the old one aside.")},
        {QStringLiteral("cover"), tr("Cover"), tr("The new page slides in over the old one.")},
        {QStringLiteral("fade"), tr("Fade"), tr("The old page fades into the new one.")},
        {QStringLiteral("curl"), tr("Page Curl"), tr("The page folds back like paper.")},
        {QStringLiteral("uncover"), tr("Uncover"), tr("The old page slides away, showing the new one under it.")},
        {QStringLiteral("flip"), tr("Hardcover Flip"), tr("The page swings over on its spine, like a stiff page in a hardcover book.")},
        {QStringLiteral("card"), tr("Card Flip"), tr("The page turns over like a card, the new page on its back.")}};
    QVariantList out;
    for (const auto& s : styles)
        out << QVariantMap{{QStringLiteral("key"), s[0]}, {QStringLiteral("name"), s[1]}, {QStringLiteral("about"), s[2]}};
    return out;
}

QVariantList MobileApp::pageTurnMotions() const
{
    const QList<std::pair<QString, QString>> motions{
        {QStringLiteral("smooth"), tr("Smooth (Fast, Then Slowing)")},
        {QStringLiteral("even"), tr("Even")},
        {QStringLiteral("gentle"), tr("Gentle (Slow Start and End)")},
        {QStringLiteral("soft"), tr("Soft (Slowing a Little)")},
        {QStringLiteral("quick"), tr("Quick (Very Fast, Long Slow Finish)")},
        {QStringLiteral("snappy"), tr("Snappy (Fast, Sudden Stop)")},
        {QStringLiteral("rounded"), tr("Rounded (Fast, Round Finish)")},
        {QStringLiteral("speeding"), tr("Speeding Up")},
        {QStringLiteral("ease"), tr("Ease In and Out")},
        {QStringLiteral("strong"), tr("Strong (Slow, Fast, Slow)")},
        {QStringLiteral("overshoot"), tr("Overshoot (Goes Past, Settles Back)")},
        {QStringLiteral("windup"), tr("Wind-Up (Pulls Back First)")},
        {QStringLiteral("springy"), tr("Springy (Wobbles Into Place)")},
        {QStringLiteral("bounce"), tr("Bounce (Bounces Into Place)")}};
    QVariantList out;
    for (const auto& [key, name] : motions) out << QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("name"), name}};
    return out;
}

void MobileApp::setTheme(const QString& key)
{
    if (key == m_settings.theme || !isThemeKey(key)) return;
    m_settings.theme = key;
    saveSettings();
    android_system::setLightSystemBars(!pageColors().dark);
    emit lookChanged();
}

MobileApp::Colors MobileApp::pageColors() const { return themeColors(m_settings.theme); }

QVariantMap MobileApp::colors() const
{
    const Colors c = pageColors();
    return {{QStringLiteral("bg"), c.bg}, {QStringLiteral("fg"), c.fg}, {QStringLiteral("muted"), c.muted},
            {QStringLiteral("accent"), c.accent}, {QStringLiteral("line"), c.line}, {QStringLiteral("surface"), c.surface},
            {QStringLiteral("pop"), c.pop}, {QStringLiteral("dark"), c.dark}};
}

bool MobileApp::hasGeminiKey() const { return m_gemini->hasKey(); }
bool MobileApp::keyStoreAvailable() const { return core::secrets::available(); }

QString MobileApp::saveGeminiKey(const QString& key)
{
    const QString k = key.trimmed();
    if (k.isEmpty()) return tr("Paste the key first.");
    if (k.size() < 20 || k.contains(QLatin1Char(' '))) return tr("That doesn't look like a Gemini API key.");
    QString err;
    const bool ok = core::secrets::write(kGeminiKey, k, &err);
    m_gemini->setKey(k);   // in use now even when the store refused it (then until the app closes)
    emit translationChanged();
    return ok ? QString() : tr("The key works until the app closes; it couldn't be kept: %1").arg(err);
}

void MobileApp::removeGeminiKey()
{
    core::secrets::remove(kGeminiKey);
    m_gemini->setKey(QString());
    emit translationChanged();
}

bool MobileApp::dictionaryInstalled() const { return m_dictionary->info().ok; }

QString MobileApp::dictionaryStatus() const
{
    if (m_dictionary->info().ok) return tr("Spanish → English: %1 words.").arg(QLocale().toString(m_dictionary->info().entries));
    return tr("Spanish → English: not installed (%1 MB download).").arg((core::DictionaryDownload::spanishEnglish().size + 500000) / 1000000);
}

void MobileApp::downloadDictionary(const QString& key)
{
    core::OfflineDictionary* dict = dictionaryFor(key);
    if (!dict) return;
    core::DictionaryDownload*& d = m_downloads[key];
    if (d && d->isRunning()) return;
    if (!d) {
        d = new core::DictionaryDownload(this);
        connect(d, &core::DictionaryDownload::progress, this, [this, key](qint64 got, qint64 total) {
            m_progressByKey[key] = total > 0 ? double(got) / double(total) : 0;
            if (key == QLatin1String("es")) m_downloadProgress = m_progressByKey[key];
            emit downloadChanged();
        });
        connect(d, &core::DictionaryDownload::finished, this, [this, key](const QString& file, const QString& error) {
            m_progressByKey.remove(key);
            if (key == QLatin1String("es")) m_downloadProgress = -1;
            emit downloadChanged();
            if (!error.isEmpty()) {
                QFile::remove(file);
                setMessage(error);
                return;
            }
            const core::DictionaryInfo info = core::OfflineDictionary::inspect(file);
            if (!info.ok || keyOf(info) != key) {
                QFile::remove(file);
                setMessage(tr("The downloaded dictionary can't be used."));
                return;
            }
            core::OfflineDictionary* target = dictionaryFor(key);
            target->close();
            QFile::remove(dictionaryFile(key));
            QFile::rename(file, dictionaryFile(key));
            target->open(dictionaryFile(key));
            setMessage(tr("The dictionary is installed."));
            emit translationChanged();
            emit downloadChanged();
        });
    }
    QDir().mkpath(QFileInfo(dictionaryFile(key)).absolutePath());
    m_progressByKey[key] = 0;
    if (key == QLatin1String("es")) m_downloadProgress = 0;
    emit downloadChanged();
    d->start(sourceFor(key), dictionaryFile(key) + QStringLiteral(".download"));
}

void MobileApp::cancelDownload(const QString& key)
{
    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it)
        if ((key.isEmpty() || it.key() == key) && it.value()) it.value()->cancel();
}

void MobileApp::removeDictionary(const QString& key)
{
    core::OfflineDictionary* d = dictionaryFor(key);
    if (!d) return;
    d->close();
    QFile::remove(dictionaryFile(key));   // the app's own copy
    emit translationChanged();
    emit downloadChanged();
}

QVariantList MobileApp::dictionaries() const
{
    QVariantList out;
    for (const DictionaryKind& k : kDictionaries) {
        const QString key = QLatin1String(k.key);
        const core::OfflineDictionary* d = dictionaryFor(key);
        const QString title = key == QLatin1String("es") ? tr("Spanish → English")
                            : key == QLatin1String("en") ? tr("English Definitions")
                                                         : tr("Spanish Definitions");
        const QString about = key == QLatin1String("es") ? tr("Single words at once, with no internet (Wiktionary).")
                            : key == QLatin1String("en") ? tr("For English books (Open English WordNet).")
                                                         : tr("For the Dictionary card in Spanish books (Spanish Wiktionary).");
        const bool installed = d && d->info().ok;
        const core::DictionaryDownload* dl = m_downloads.value(key);
        const qint64 size = sourceFor(key).size;
        QVariantMap m;
        m[QStringLiteral("key")] = key;
        m[QStringLiteral("title")] = title;
        m[QStringLiteral("about")] = about;
        m[QStringLiteral("installed")] = installed;
        m[QStringLiteral("downloading")] = dl && dl->isRunning();
        m[QStringLiteral("progress")] = m_progressByKey.value(key, -1);
        m[QStringLiteral("sizeMb")] = int((size + 500000) / 1000000);
        m[QStringLiteral("status")] = installed ? tr("%1 words.").arg(QLocale().toString(d->info().entries)) : tr("Not installed.");
        out << m;
    }
    return out;
}

QVariantList MobileApp::themes() const
{
    QVariantList out;
    for (const ThemeDef& d : themeTable()) {
        const Theme t = Theme::fromDef(d);
        out << QVariantMap{{QStringLiteral("key"), t.key}, {QStringLiteral("name"), t.name}, {QStringLiteral("group"), t.group},
                           {QStringLiteral("bg"), t.bg}, {QStringLiteral("fg"), t.fg}, {QStringLiteral("accent"), t.accent}};
    }
    return out;
}

// ---------------------------------------------------------------- the Dictionary card

bool MobileApp::showDictionary(QObject* object)
{
    auto* page = qobject_cast<BookPage*>(object);
    if (!page || !page->hasSelection()) return false;
    const QList<core::OfflineDictionary*> all{m_definitionsEs, m_dictionaryEn, m_dictionary};
    const QString text = page->selectedText(), book = languageCode();
    const core::FoundCard f = core::findDictionaryCard(all, text, book);
    if (!f.found()) return false;
    const Colors c = pageColors();
    auto side = [&](const core::FoundCard& card) {
        return QVariantMap{{QStringLiteral("title"), card.title.toUpper()},
                           {QStringLiteral("html"), core::dictionaryCardHtml(card.card, c.fg, c.muted, c.line)},
                           {QStringLiteral("source"), card.source},
                           {QStringLiteral("url"), card.fullDefinition.toString(QUrl::FullyEncoded)},
                           {QStringLiteral("offer"), card.offerDefinitions},
                           {QStringLiteral("word"), card.card.headwords.first().word}};
    };
    // A book in another language: two sides, as on the desktop: the book's language first (its dictionary of
    // definitions, or why there is none), then the reader's (Flip turns the card over).
    QVariantList sides;
    const core::FoundCard defs = core::findDictionaryCard(all, text, book, core::DictionaryKind::Definitions);
    const core::FoundCard trans = core::findDictionaryCard(all, text, book, core::DictionaryKind::Translating);
    if (trans.found() && trans.dictionary->info().target != book) {
        if (defs.found()) {
            sides << side(defs);
        } else {
            const bool installed = m_definitionsEs->info().ok && book == QLatin1String("es");
            sides << QVariantMap{{QStringLiteral("title"), tr("SPANISH DEFINITIONS")},
                                 {QStringLiteral("html"), QStringLiteral("<p>%1</p>").arg((installed ? tr("The Spanish definitions don't have this word.")
                                                                                                    : tr("The Spanish definitions aren't installed.")).toHtmlEscaped())},
                                 {QStringLiteral("offer"), !installed && book == QLatin1String("es")},
                                 {QStringLiteral("word"), trans.card.headwords.first().word}};
        }
        sides << side(trans);
    } else {
        sides << side(f);
    }
    m_card = sides.first().toMap();
    m_card[QStringLiteral("found")] = true;
    m_card[QStringLiteral("sides")] = sides;
    m_card[QStringLiteral("side")] = 0;
    emit cardChanged();
    return true;
}

void MobileApp::flipCard()
{
    const QVariantList sides = m_card.value(QStringLiteral("sides")).toList();
    if (sides.size() < 2) return;
    const int next = 1 - m_card.value(QStringLiteral("side")).toInt();
    QVariantMap m = sides[next].toMap();
    m[QStringLiteral("found")] = true;
    m[QStringLiteral("sides")] = sides;
    m[QStringLiteral("side")] = next;
    m_card = m;
    emit cardChanged();
}

void MobileApp::closeCard()
{
    if (m_card.isEmpty()) return;
    m_card.clear();
    emit cardChanged();
}

// ---------------------------------------------------------------- highlights, notes, bookmarks

int MobileApp::annotationAt(QObject* object) const
{
    auto* page = qobject_cast<BookPage*>(object);
    if (!page || !m_notes || !m_bookId || !page->hasSelection()) return 0;
    for (const core::Annotation& a : m_notes->highlightsAt(m_bookId, page->selectionSpine(), page->selectionStart(), page->selectionEnd()))
        if (a.start <= page->selectionStart() && a.end >= page->selectionEnd()) return a.id;
    return 0;
}

int MobileApp::highlight(QObject* object, const QString& color)
{
    auto* page = qobject_cast<BookPage*>(object);
    if (!page || !m_notes || !m_bookId || !page->hasSelection()) return 0;
    const int spine = page->selectionSpine();
    const QString sentence = page->selectionSentence();
    const core::HighlightChange c = core::highlightRange(*m_notes, *m_engine, m_bookId, spine, page->selectionStart(),
                                                         page->selectionEnd(), color, chapterTitleAt({spine, page->selectionStart()}),
                                                         page->selectedText());
    // A highlighted word or short phrase joins the Word List (no lookup: the offline dictionary's meaning, if any).
    if (c.id && !c.recolored && m_words && m_settings.practiceHighlights) {
        const QString text = core::vocabularyText(c.text);
        if (!text.isEmpty()) {
            core::TranslationResult meaning = m_translator->offlineMeaning(text, languageCode());
            meaning.text = text;
            core::WordPlace place;
            place.bookId = m_bookId;
            place.bookTitle = m_title;
            place.language = languageCode();
            place.spine = spine;
            place.start = c.from;
            place.end = c.to;
            place.sentence = core::contextAround(sentence, text);
            const core::Position at{spine, c.from};
            place.cfi = m_engine->cfiFor(at);
            if (const auto ix = m_engine->index()) place.pageLabel = ix->pageLabel(at);
            place.chapter = chapterTitleAt(at);
            if (m_words->mark(meaning, place)) emit wordsChanged();
        }
    }
    // A color used (not one tapped again to take a highlight away) moves to the front of the dots.
    if (c.id) {
        m_settings.highlightOrder = core::Annotations::usedColor(m_settings.highlightOrder, color);
        saveSettings();
        emit highlightColorsChanged();
    }
    page->clearSelection();
    emit annotationsChanged();
    return c.id;
}

QString MobileApp::highlightColorAt(QObject* page) const
{
    const int id = annotationAt(page);
    return id ? m_notes->get(id).color : QString();
}

QString MobileApp::noteAt(QObject* page) const
{
    const int id = annotationAt(page);
    return id ? m_notes->get(id).note : QString();
}

int MobileApp::setNote(QObject* object, const QString& note)
{
    auto* page = qobject_cast<BookPage*>(object);
    if (!page || !m_notes || !page->hasSelection()) return 0;
    int id = annotationAt(page);
    if (!id) {
        // A note goes on a highlight: yellow when the words aren't highlighted yet.
        const int spine = page->selectionSpine();
        id = core::highlightRange(*m_notes, *m_engine, m_bookId, spine, page->selectionStart(), page->selectionEnd(),
                                  QStringLiteral("yellow"), chapterTitleAt({spine, page->selectionStart()}), page->selectedText()).id;
    }
    if (!id) return 0;
    core::Annotation a = m_notes->get(id);
    a.note = note.trimmed();
    m_notes->update(a);
    page->clearSelection();
    emit annotationsChanged();
    return id;
}

void MobileApp::removeHighlightAt(QObject* object)
{
    auto* page = qobject_cast<BookPage*>(object);
    const int id = annotationAt(object);
    if (!id) return;
    m_notes->remove(id);
    if (page) page->clearSelection();
    emit annotationsChanged();
}

namespace {

// The end of the screen that starts at `p` (its chapter must be laid out), else `p` itself: two pages when two are
// side by side (the reading surface reports it).
int pageEnd(core::BookEngine* engine, const core::Position& p, const core::Position& screenEnd = {-1, 0})
{
    if (screenEnd.spine == p.spine && screenEnd.offset > p.offset) return screenEnd.offset;
    const auto cl = engine->layout(p.spine);
    if (!cl) return p.offset;
    const int page = cl->pageForOffset(p.offset);
    return page >= 0 && page < int(cl->pages.size()) ? cl->pages[size_t(page)].end : p.offset;
}

}  // namespace

void MobileApp::toggleBookmark()
{
    if (!m_notes || !m_bookId) return;
    const int end = pageEnd(m_engine, m_position, m_screenEnd);
    if (const int existing = m_notes->bookmarkAt(m_bookId, m_position.spine, m_position.offset, qMax(m_position.offset, end - 1))) {
        m_notes->remove(existing);
    } else {
        const auto cl = m_engine->layout(m_position.spine);
        core::Annotation a;
        a.bookId = m_bookId;
        a.kind = core::Annotation::Bookmark;
        a.spine = m_position.spine;
        a.start = a.end = m_position.offset;
        a.text = cl ? cl->flow->textRange(m_position.offset, qMin(end, m_position.offset + 120)).simplified() : QString();
        a.cfi = m_engine->cfiFor(m_position);
        if (const auto ix = m_engine->index()) a.pageLabel = ix->pageLabel(m_position);
        a.chapter = chapterTitleAt(m_position);
        const int newId = m_notes->add(a);
        emit annotationsChanged();
        // Remove the bookmark before it? (as on the desktop, the owner's request of 2026-11-26: a bookmark usually
        // marks where reading got to, and the old one is then in the way). The question is the screen's to show.
        QList<core::Annotation> earlier;
        for (const core::Annotation& b : m_notes->forBook(m_bookId))
            if (b.kind == core::Annotation::Bookmark && b.id != newId) earlier << b;
        if (earlier.isEmpty()) return;
        std::sort(earlier.begin(), earlier.end(), [](const core::Annotation& x, const core::Annotation& y) {
            return x.created != y.created ? x.created > y.created : x.id > y.id;
        });
        m_earlierBookmarks.clear();
        for (const core::Annotation& b : earlier) m_earlierBookmarks << b.id;
        const core::Annotation& last = earlier.first();
        const QString where = !last.pageLabel.isEmpty() ? tr("page %1").arg(last.pageLabel) : QStringLiteral("“%1…”").arg(last.text.left(40).trimmed());
        const int n = int(earlier.size());
        emit bookmarkQuestion(n == 1 ? tr("This book also has a bookmark on %1. Remove it?").arg(where)
                                     : tr("This book also has %1 other bookmarks; the last one added is on %2. Remove it?").arg(QLocale().toString(n), where),
                              n);
        return;
    }
    emit annotationsChanged();
}

void MobileApp::removeEarlierBookmarks(bool all)
{
    // The answer to bookmarkQuestion: the bookmark added last before the new one, or all of them.
    if (!m_notes || m_earlierBookmarks.isEmpty()) return;
    if (all) for (int id : std::as_const(m_earlierBookmarks)) m_notes->remove(id);
    else m_notes->remove(m_earlierBookmarks.first());
    m_earlierBookmarks.clear();
    emit annotationsChanged();
}

void MobileApp::goToLatestBookmark()
{
    // The bookmark added last, in any book: its book opened when another one (or none) is open.
    if (!m_notes) return;
    const core::Annotation a = m_notes->latestBookmark();
    if (a.id == 0) {
        setMessage(tr("No bookmarks yet."));
        return;
    }
    if (a.bookId == m_bookId) {
        goToAnnotation(a.id);
        return;
    }
    m_pendingWord = {a.bookId, a.spine, a.start, core::Annotations::isWordBookmark(a) ? a.end : a.start, a.cfi};
    openBook(a.bookId);
}

bool MobileApp::hasBookmarks() const { return m_notes && m_notes->latestBookmark().id != 0; }

bool MobileApp::pageBookmarked() const
{
    if (!m_notes || !m_bookId) return false;
    const int end = pageEnd(m_engine, m_position, m_screenEnd);
    return m_notes->bookmarkAt(m_bookId, m_position.spine, m_position.offset, qMax(m_position.offset, end - 1)) != 0;
}

bool MobileApp::wordBookmarkAt(QObject* object) const
{
    auto* page = qobject_cast<BookPage*>(object);
    if (!page || !m_notes || !m_bookId || !page->hasSelection()) return false;
    return m_notes->wordBookmarkAt(m_bookId, page->selectionSpine(), page->selectionStart(), page->selectionEnd()) != 0;
}

void MobileApp::toggleWordBookmark(QObject* object)
{
    auto* page = qobject_cast<BookPage*>(object);
    if (!page || !m_notes || !m_bookId || !page->hasSelection()) return;
    const int spine = page->selectionSpine();
    if (const int existing = m_notes->wordBookmarkAt(m_bookId, spine, page->selectionStart(), page->selectionEnd())) {
        m_notes->remove(existing);
    } else {
        int from = 0, to = 0;
        core::firstWord(*m_engine, spine, page->selectionStart(), page->selectionEnd(), &from, &to);
        if (to <= from) return;
        core::Annotation b;
        b.bookId = m_bookId;
        b.kind = core::Annotation::Bookmark;
        b.spine = spine;
        b.start = from;
        b.end = to;
        if (const auto cl = m_engine->layout(spine)) b.text = cl->flow->textRange(from, qMin(cl->flow->length, from + 120)).simplified();
        b.cfi = m_engine->cfiFor({spine, from});
        if (const auto ix = m_engine->index()) b.pageLabel = ix->pageLabel({spine, from});
        b.chapter = chapterTitleAt({spine, from});
        m_notes->add(b);
    }
    page->clearSelection();
    emit annotationsChanged();
}

std::vector<std::pair<int, int>> MobileApp::wordBookmarksIn(int spine) const
{
    std::vector<std::pair<int, int>> out;
    if (!m_notes || !m_bookId) return out;
    for (const core::Annotation& a : m_notes->inChapter(m_bookId, spine))
        if (core::Annotations::isWordBookmark(a)) out.push_back({a.start, a.end});
    return out;
}

void MobileApp::removeAnnotation(int id)
{
    if (!m_notes) return;
    m_notes->remove(id);
    emit annotationsChanged();
}

void MobileApp::goToAnnotation(int id)
{
    if (!m_notes) return;
    const core::Annotation a = m_notes->get(id);
    if (a.id == 0 || a.bookId != m_bookId) return;
    core::Position p{a.spine, a.start};
    if (!a.cfi.isEmpty()) {
        core::Position fromCfi;
        if (m_engine->positionForCfi(a.cfi, &fromCfi)) p = fromCfi;
    }
    jumpTo(p);
    if (a.kind == core::Annotation::Highlight || core::Annotations::isWordBookmark(a))
        emit selectRequested(p.spine, p.offset, p.offset + (a.end - a.start));
}

void MobileApp::copyText(const QString& text)
{
    if (QClipboard* c = QGuiApplication::clipboard()) c->setText(text);
}

QVariantList MobileApp::notebook() const
{
    QVariantList out;
    if (!m_notes || !m_bookId) return out;
    for (const core::Annotation& a : m_notes->forBook(m_bookId)) {
        const bool hl = a.kind == core::Annotation::Highlight;
        out << QVariantMap{{QStringLiteral("id"), a.id},
                           {QStringLiteral("kind"), hl ? QStringLiteral("highlight") : QStringLiteral("bookmark")},
                           {QStringLiteral("word"), core::Annotations::isWordBookmark(a)},
                           {QStringLiteral("color"), a.color},
                           {QStringLiteral("swatch"), hl ? core::Annotations::colorValue(a.color) : pageColors().accent},
                           {QStringLiteral("text"), a.text},
                           {QStringLiteral("note"), a.note},
                           {QStringLiteral("page"), a.pageLabel},
                           {QStringLiteral("chapter"), a.chapter}};
    }
    return out;
}

QVariantList MobileApp::highlightColors() const
{
    QVariantList out;
    for (const QString& c : core::Annotations::colorsInOrder(m_settings.highlightOrder))
        out << QVariantMap{{QStringLiteral("key"), c}, {QStringLiteral("swatch"), core::Annotations::colorValue(c)}};
    return out;
}

std::vector<layout::Overlay> MobileApp::highlightsIn(int spine) const
{
    std::vector<layout::Overlay> out;
    if (!m_notes || !m_bookId) return out;
    const bool dark = pageColors().dark;
    for (const core::Annotation& a : m_notes->inChapter(m_bookId, spine)) {
        if (a.kind != core::Annotation::Highlight) continue;
        QColor c = core::Annotations::colorValue(a.color);
        c.setAlphaF(dark ? 0.34f : 0.42f);
        out.push_back({a.start, a.end, c});
    }
    return out;
}

// ---------------------------------------------------------------- contents and search

QVariantList MobileApp::contents() const
{
    QVariantList out;
    const core::Book* book = m_engine->book();
    if (!book) return out;
    const auto ix = m_engine->index();
    const int current = ix ? ix->tocEntryAt(m_position) : -1;
    for (size_t i = 0; i < book->toc.size(); ++i)
        out << QVariantMap{{QStringLiteral("title"), book->toc[i].title.simplified()},
                           {QStringLiteral("depth"), book->toc[i].depth},
                           {QStringLiteral("current"), int(i) == current}};
    return out;
}

void MobileApp::goToContents(int index)
{
    const auto ix = m_engine->index();
    if (!ix || index < 0 || index >= int(ix->toc.size()) || ix->toc[size_t(index)].spine < 0) return;
    jumpTo({ix->toc[size_t(index)].spine, ix->toc[size_t(index)].offset});
}

// ---------------------------------------------------------------- moving around

void MobileApp::jumpTo(const core::Position& p)
{
    if (!m_bookId) return;
    if (p != m_position) {
        m_history.push_back(m_position);
        if (m_history.size() > 50) m_history.erase(m_history.begin());
        emit historyChanged();
    }
    emit jumpRequested(p.spine, p.offset);
}

void MobileApp::forgetHistory()
{
    if (m_history.empty()) return;
    m_history.clear();
    emit historyChanged();
}

QString MobileApp::backText() const
{
    if (m_history.empty() || !m_bookId) return {};
    const auto ix = m_engine->index();
    return ix ? tr("Back to Page %1").arg(ix->pageLabel(m_history.back())) : tr("Back");
}

void MobileApp::goBack()
{
    if (m_history.empty()) return;
    const core::Position p = m_history.back();
    m_history.pop_back();
    emit historyChanged();
    emit jumpRequested(p.spine, p.offset);
}

QString MobileApp::pageOrPercentHint() const
{
    const auto ix = m_engine->index();
    if (!ix) return tr("The book is still being prepared. Try again in a moment.");
    return tr("Page 1 to %1, or a percentage such as 45%").arg(ix->lastPageLabel());
}

QString MobileApp::goToPageOrPercent(const QString& raw)
{
    // The desktop's rules: "45%" (or "45,5 %"), a printed page label ("xii") when the book has them, or a page.
    const auto ix = m_engine->index();
    const QString text = raw.trimmed();
    if (!ix || !m_bookId) return tr("The book is still being prepared. Try again in a moment.");
    const QString usage = tr("Type a page number from 1 to %1, or a percentage such as 45%.").arg(ix->lastPageLabel());
    if (text.isEmpty()) return usage;
    static const QRegularExpression pct(QStringLiteral("^(\\d+(?:[.,]\\d+)?)\\s*%$"));
    const auto m = pct.match(text);
    if (m.hasMatch()) {
        const double v = QString(m.captured(1)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        if (v < 0 || v > 100) return usage;
        jumpTo(ix->fromGlobal(qint64(std::ceil(v / 100.0 * double(ix->total)))));
        return {};
    }
    for (const auto& printed : ix->printedPages)
        if (printed.label.compare(text, Qt::CaseInsensitive) == 0) {
            jumpTo(ix->fromGlobal(printed.global));
            return {};
        }
    bool ok = false;
    int page = QLocale().toInt(text, &ok);
    if (!ok) page = text.toInt(&ok);
    if (!ok || page < 1 || page > ix->pageCount()) return usage;
    jumpTo(ix->positionOfPage(page));
    return {};
}

void MobileApp::nextChapter()
{
    const auto ix = m_engine->index();
    if (!ix || !m_bookId) return;
    const qint64 g = ix->global(m_position);
    qint64 best = -1;
    core::Position target;
    for (const auto& t : ix->toc)
        if (t.spine >= 0 && t.global > g && (best < 0 || t.global < best)) {
            best = t.global;
            target = {t.spine, t.offset};
        }
    if (best >= 0) jumpTo(target);
    else setMessage(tr("This is the last chapter."));
}

void MobileApp::previousChapter()
{
    const auto ix = m_engine->index();
    if (!ix || !m_bookId) return;
    const qint64 g = ix->global(m_position);
    qint64 best = -1;
    core::Position target;
    for (const auto& t : ix->toc)
        if (t.spine >= 0 && t.global < g - 1 && t.global > best) {
            best = t.global;
            target = {t.spine, t.offset};
        }
    jumpTo(best >= 0 ? target : core::Position{0, 0});
}

void MobileApp::goToBeginning() { jumpTo({0, 0}); }

void MobileApp::followLink(const QString& target, bool external, bool noteRef)
{
    if (external) {
        openLink(target);
        return;
    }
    const auto ix = m_engine->index();
    const core::Book* book = m_engine->book();
    core::Position p;
    if (!ix || !book || !ix->positionFor(*book, target, &p)) return;
    const core::NoteText note = noteRef || target.contains(QLatin1Char('#')) ? m_engine->noteText(target) : core::NoteText{};
    if ((noteRef || note.isNote) && !note.text.isEmpty()) {
        m_footnoteTarget = p;
        emit footnoteShown(note.text);
        return;
    }
    jumpTo(p);
}

void MobileApp::goToFootnote()
{
    if (m_footnoteTarget.spine < 0) return;
    jumpTo(m_footnoteTarget);
}

// ---------------------------------------------------------------- Show English Alongside

void MobileApp::setAlongside(bool on)
{
    if (m_settings.showBilingual == on) return;
    m_settings.showBilingual = on;
    saveSettings();
    m_alongsideEnglish.clear();
    m_alongsideStatus.clear();
    emit alongsideChanged();
    if (on) refreshAlongside();
}

QList<MobileApp::Paragraph> MobileApp::visibleParagraphs() const
{
    // As the desktop: the text blocks the screen shows (one begun on the page before included); page numbers,
    // ornaments and "* * *" left out.
    QList<Paragraph> out;
    if (!m_bookId || !m_engine->isOpen()) return out;
    const core::Position a = m_screenStart.spine >= 0 ? m_screenStart : m_position;
    const core::Position e = m_screenEnd;
    const auto cl = m_engine->layout(a.spine);
    if (!cl) return out;
    for (const layout::Block& b : cl->flow->blocks) {
        if (b.kind != layout::Block::Text) continue;
        if (b.start + b.length <= a.offset) continue;
        if (e.spine == a.spine && b.start >= e.offset) break;
        QString t = b.text;
        t.replace(QChar(0x2028), QLatin1Char(' '));
        t = t.simplified();
        int letters = 0;
        for (const QChar c : t)
            if (c.isLetter()) ++letters;
        if (letters < 2) continue;
        out << Paragraph{b.start, t};
    }
    return out;
}

QString MobileApp::paragraphKey(const QString& text) const
{
    const QByteArray raw = (QStringLiteral("paragraph|") + languageCode() + QLatin1Char('|') + core::languages::readerLanguage() +
                            QLatin1Char('|') + text).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
}

QVariantMap MobileApp::alongsideText() const
{
    QVariantList rows;
    const QList<Paragraph> paras = m_settings.showBilingual ? visibleParagraphs() : QList<Paragraph>();
    for (int i = 0; i < paras.size(); ++i)
        rows << QVariantMap{{QStringLiteral("text"), paras[i].text}, {QStringLiteral("english"), i < m_alongsideEnglish.size() ? m_alongsideEnglish[i] : QString()}};
    return {{QStringLiteral("title"), core::languages::displayName(core::languages::readerLanguage())},
            {QStringLiteral("status"), m_alongsideStatus},
            {QStringLiteral("waiting"), !m_alongsideAsked.isEmpty()},   // "…" for the paragraphs still coming
            {QStringLiteral("paragraphs"), rows}};
}

void MobileApp::refreshAlongside()
{
    m_alongsideTimer.stop();
    if (!m_settings.showBilingual || !m_bookId) return;
    const QList<Paragraph> paras = visibleParagraphs();
    m_alongsideEnglish.clear();
    if (paras.isEmpty()) {
        m_alongsideStatus = tr("No text on this page.");
        emit alongsideChanged();
        return;
    }
    QStringList missing;
    for (const Paragraph& p : paras) {
        QString t = m_library ? m_library->cachedTranslation(paragraphKey(p.text)).value(QLatin1String("translation")).toString() : QString();
        if (t.isEmpty()) t = m_alongsideOffline.value(p.text);
        m_alongsideEnglish << t;
        if (t.isEmpty() && !missing.contains(p.text) && !m_alongsideGaveUp.contains(p.text)) missing << p.text;
    }
    m_alongsideStatus.clear();
    for (const QStringList& asked : std::as_const(m_alongsideAsked))
        for (const QString& t : asked) missing.removeAll(t);
    if (!missing.isEmpty()) {
        // The screen's paragraphs in one request (a very long page in a few).
        core::TranslationRequest r;
        r.kind = core::TranslationRequest::Kind::Paragraphs;
        r.from = languageCode();
        for (const QString& t : std::as_const(missing)) {
            r.paragraphs << (t.size() > 4000 ? t.left(4000) : t);
            if (r.paragraphs.size() == 25) break;
        }
        r.text = r.paragraphs.first().left(200);
        applyTranslationSettings();
        m_translator->setOfflineOnly(bookOffline());
        const int id = m_translator->translate(r);
        m_alongsideAsked.insert(id, r.paragraphs);
    }
    if (!m_alongsideAsked.isEmpty() && m_alongsideEnglish.contains(QString())) m_alongsideStatus = tr("Translating this page…");
    emit alongsideChanged();
}

void MobileApp::onAlongsideTranslated(int id, const core::TranslationResult& r, const QString& why)
{
    const QStringList asked = m_alongsideAsked.take(id);
    if (r.paragraphs.size() == asked.size() && !asked.isEmpty()) {
        for (int i = 0; i < asked.size(); ++i) {
            const QString english = r.paragraphs[i].trimmed();
            if (english.isEmpty()) {
                m_alongsideGaveUp.insert(asked[i]);   // not asked again and again
                continue;
            }
            // Gemini's English is kept (in the library, as on the desktop); an offline engine's only for now.
            if (r.keep && m_library)
                m_library->cacheTranslation(paragraphKey(asked[i]), r.provider,
                                            QJsonObject{{QStringLiteral("translation"), english}, {QStringLiteral("provider"), r.provider}});
            else m_alongsideOffline.insert(asked[i], english);
        }
        refreshAlongside();
        return;
    }
    if (!m_settings.showBilingual) return;
    // Nothing could translate the page: what is there stays, and why.
    const QString mine = core::languages::displayName(core::languages::readerLanguage());
    QString text = !m_settings.useGemini || !m_gemini->hasKey()
                       ? tr("Add a Gemini key and turn Gemini on (Settings) to see the %1 of whole paragraphs.").arg(mine)
                       : tr("The %1 for this page couldn't be fetched.").arg(mine) + (why.isEmpty() ? QString() : QLatin1Char('\n') + why);
    if (bookOffline() && m_settings.useGemini) text += QLatin1Char('\n') + tr("This book is kept offline, so Gemini isn't asked.");
    else if (m_settings.useGemini && m_settings.geminiExplainOnly) text += QLatin1Char('\n') + tr("Gemini is used only for Explain This Sentence (Settings).");
    m_alongsideStatus = text;
    emit alongsideChanged();
}

void MobileApp::showImage(const QString& path)
{
    QImage img;
    if (!img.loadFromData(m_engine->resource(path)) || img.isNull()) return;
    m_image = img;
    m_imageShown = QStringLiteral("image://book/%1").arg(++m_imageSerial);
    emit imageChanged();
}

void MobileApp::closeImage()
{
    if (m_imageShown.isEmpty()) return;
    m_imageShown.clear();
    m_image = QImage();
    emit imageChanged();
}

namespace {
class BookImageProvider : public QQuickImageProvider {
public:
    explicit BookImageProvider(MobileApp* app) : QQuickImageProvider(QQuickImageProvider::Image), m_app(app) {}
    QImage requestImage(const QString&, QSize* size, const QSize&) override
    {
        const QImage img = m_app ? m_app->imageForProvider() : QImage();
        if (size) *size = img.size();
        return img;
    }

private:
    QPointer<MobileApp> m_app;
};
}  // namespace

QQuickImageProvider* MobileApp::newImageProvider() { return new BookImageProvider(this); }

void MobileApp::search(const QString& query, bool wholeWord, bool matchCase, bool matchAccents)
{
    m_hits.clear();
    m_searchSummary.clear();
    if (!m_bookId || query.trimmed().size() < 2) {
        m_engine->cancelSearch();
        m_searchId = 0;
        emit searchChanged();
        return;
    }
    core::SearchOptions o;
    o.wholeWord = wholeWord;
    o.matchCase = matchCase;
    o.matchAccents = matchAccents;
    m_searchId = m_engine->search(query.trimmed(), o);
    m_searchSummary = tr("Searching…");
    emit searchChanged();
}

void MobileApp::goToSearchResult(int index)
{
    if (index < 0 || index >= m_hits.size()) return;
    jumpTo({m_hits[index].spine, m_hits[index].start});
    emit selectRequested(m_hits[index].spine, m_hits[index].start, m_hits[index].end);
}

void MobileApp::searchAll(const QString& query, bool wholeWord, bool matchCase, bool matchAccents)
{
    m_allRows.clear();
    m_allUnreadable = 0;
    const QString q = query.simplified();
    if (q.size() < 2) {
        cancelSearchAll();
        m_allSummary = q.isEmpty() ? QString() : tr("Type at least 2 letters.");
        emit allSearchChanged();
        return;
    }
    QList<core::LibrarySearch::BookRef> books;
    if (m_library)
        for (const core::LibraryBook& b : m_library->books()) books << core::LibrarySearch::BookRef{b.id, b.path, b.title, b.author};
    if (books.isEmpty()) {
        m_allSummary = tr("The library has no books yet.");
        emit allSearchChanged();
        return;
    }
    core::SearchOptions o;
    o.wholeWord = wholeWord;
    o.matchCase = matchCase;
    o.matchAccents = matchAccents;
    m_allQuery = q;
    m_allSummary = tr("Searching %1…").arg(countText(int(books.size()), tr("book"), tr("books")));
    m_allSearchId = m_librarySearch->start(books, q, o);
    emit allSearchChanged();
}

void MobileApp::cancelSearchAll()
{
    if (m_librarySearch->isRunning()) m_librarySearch->cancel();
}

QVariantList MobileApp::allResults() const
{
    QVariantList out;
    for (const AllRow& r : m_allRows) {
        if (r.isHit)
            out << QVariantMap{{QStringLiteral("kind"), QStringLiteral("hit")}, {QStringLiteral("before"), r.hit.before},
                               {QStringLiteral("match"), r.hit.match}, {QStringLiteral("after"), r.hit.after}};
        else
            out << QVariantMap{{QStringLiteral("kind"), QStringLiteral("book")}, {QStringLiteral("title"), r.title},
                               {QStringLiteral("author"), r.author},
                               {QStringLiteral("count"), r.truncated ? tr("first %1").arg(QLocale().toString(r.count)) : QLocale().toString(r.count)}};
    }
    return out;
}

void MobileApp::openAllResult(int row)
{
    if (row < 0 || row >= int(m_allRows.size()) || !m_allRows[size_t(row)].isHit) return;
    const AllRow& r = m_allRows[size_t(row)];
    if (r.bookId == m_bookId) {
        jumpTo({r.hit.spine, r.hit.start});
        emit selectRequested(r.hit.spine, r.hit.start, r.hit.end);
        return;
    }
    m_pendingWord = {r.bookId, r.hit.spine, r.hit.start, r.hit.end, QString()};
    openBook(r.bookId);
}

QVariantList MobileApp::searchResults() const
{
    QVariantList out;
    const auto ix = m_engine->index();
    for (const core::SearchHit& h : m_hits)
        out << QVariantMap{{QStringLiteral("before"), h.before}, {QStringLiteral("match"), h.match}, {QStringLiteral("after"), h.after},
                           {QStringLiteral("page"), ix ? ix->pageLabel({h.spine, h.start}) : QString()}};
    return out;
}

void MobileApp::openLink(const QString& url)
{
    m_lastOpened = url;
    if (!m_quiet) QDesktopServices::openUrl(QUrl(url));
}

QUrl MobileApp::reportUrl() const
{
    // A new issue on FreeReader's GitHub page, filled in with the answer being reported; the reader reads it, can
    // change it, and sends it there (or not).
    const QString text = m_lookup.value(QStringLiteral("text")).toString();
    const QString answer = m_lookup.value(QStringLiteral("translation")).toString();
    const QString body = tr("What is wrong with this answer?\n\n\n---\nSelected text: %1\nAnswer: %2\nFrom: %3\nFreeReader %4 (phone)")
                             .arg(text.left(500), answer.left(1000), m_lookup.value(QStringLiteral("provider")).toString(), version());
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"), tr("Report: the answer for “%1”").arg(text.left(60)));
    q.addQueryItem(QStringLiteral("body"), body);
    QUrl url(QStringLiteral("https://github.com/cincottash/FreeReader/issues/new"));
    url.setQuery(q);
    return url;
}

void MobileApp::reportAnswer()
{
    if (m_lookup.value(QStringLiteral("ai")).toBool()) openLink(reportUrl().toString());
}

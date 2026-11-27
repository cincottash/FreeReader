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
#include "core/services/Secrets.h"
#include "core/services/WordLog.h"
#include "core/translate/AiProviders.h"
#include "core/translate/DictionaryCardHtml.h"
#include "core/translate/DictionaryDownload.h"
#include "core/translate/OfflineDictionary.h"
#include "core/translate/SavedWords.h"
#include "core/translate/TranslationService.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QRegularExpression>

namespace {

const QString kGeminiKey = QStringLiteral("gemini-api-key");

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
    default: return {};
    }
}

QHash<int, QByteArray> BookListModel::roleNames() const
{
    return {{IdRole, "bookId"}, {TitleRole, "title"}, {AuthorRole, "author"}, {ProgressRole, "progress"},
            {HasCoverRole, "hasCover"}, {LanguageRole, "language"}, {PageRole, "page"}};
}

void BookListModel::refresh()
{
    beginResetModel();
    m_rows.clear();
    if (m_library) {
        QList<core::LibraryBook> books = m_library->books();
        std::sort(books.begin(), books.end(), [](const core::LibraryBook& a, const core::LibraryBook& b) {
            const QDateTime ta = a.lastOpened.isValid() ? a.lastOpened : a.added, tb = b.lastOpened.isValid() ? b.lastOpened : b.added;
            return ta > tb;
        });
        for (const core::LibraryBook& b : books)
            m_rows.append({b.id, b.title, b.author, b.language, b.pageLabel, b.progress, b.hasCover});
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
                                        : tr("%n match(es).", nullptr, int(hits.size()));
        emit searchChanged();
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
    // A word or passage Gemini hasn't answered by then (2.5 s, as on the desktop) is answered by the next engine;
    // Gemini's late answer is kept for next time (the owner's rule, 2026-11-26).
    m_translator->setWait(m_gemini->name(), qMax(0, m_settings.geminiWaitMs));
    // One answer per lookup, from the first engine that can give it (as on the desktop).
    connect(m_translator, &core::TranslationService::finished, this, &MobileApp::onTranslated);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1500);
    connect(&m_saveTimer, &QTimer::timeout, this, &MobileApp::savePosition);
    connect(this, &MobileApp::annotationsChanged, this, &MobileApp::bookmarkChanged);
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
    m_position = m_start;
    m_model->refresh();
    emit readingChanged();
    emit locationChanged();
    emit annotationsChanged();
    emit contentsChanged();
    emit bookOpened();
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
    closeLookup();
    closeCard();
    m_engine->cancelSearch();
    m_searchId = 0;
    m_hits.clear();
    m_searchSummary.clear();
    emit searchChanged();
    emit annotationsChanged();
    emit contentsChanged();
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
    if (id != m_lookupId) return;
    QVariantMap m;
    m[QStringLiteral("text")] = m_lookup.value(QStringLiteral("text"));
    m[QStringLiteral("isWord")] = m_lookup.value(QStringLiteral("isWord"));
    m[QStringLiteral("loading")] = false;
    if (r.isEmpty()) {
        QString text = why.isEmpty() ? tr("No translation engine is set up yet.") : why;
        if (!m_dictionary->info().ok) text += QLatin1Char('\n') + tr("Tip: add a Gemini key, or download the offline dictionary, in Settings.");
        m[QStringLiteral("nothing")] = text;
    } else {
        m[QStringLiteral("translation")] = r.translation;
        m[QStringLiteral("alternatives")] = r.alternatives.join(QStringLiteral(", "));
        m[QStringLiteral("partOfSpeech")] = r.partOfSpeech;
        m[QStringLiteral("lemma")] = r.lemma;
        m[QStringLiteral("definition")] = r.definition;
        m[QStringLiteral("inSentence")] = r.inSentence;
        m[QStringLiteral("note")] = r.note;
        m[QStringLiteral("source")] = r.provider.isEmpty() ? QString() : tr("Source: %1").arg(r.provider);
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
    QVariantList out;
    if (!m_words) return out;
    for (const core::WordEntry& w : m_words->words()) {
        out << QVariantMap{{QStringLiteral("id"), w.id},
                           {QStringLiteral("headword"), w.headword()},
                           {QStringLiteral("text"), w.text},
                           {QStringLiteral("translation"), w.translation},
                           {QStringLiteral("partOfSpeech"), w.partOfSpeech},
                           {QStringLiteral("sentence"), w.sentence},
                           {QStringLiteral("book"), w.bookTitle},
                           {QStringLiteral("page"), w.pageLabel},
                           {QStringLiteral("starred"), w.starred},
                           {QStringLiteral("needsMeaning"), w.translation.isEmpty()}};
    }
    return out;
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

core::WordLog::Filter practiceFilter(const Settings& s)
{
    core::WordLog::Filter f;
    f.directions = s.practiceDirections.isEmpty() ? QStringList{QStringLiteral("recognize")} : s.practiceDirections;
    f.passages = s.practicePassages;
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
    m[QStringLiteral("cards")] = m_words->practisableCount(practiceFilter(m_settings));
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
    const bool ok = m_session->start(practiceFilter(m_settings), m_settings.practiceOrder, QDateTime::currentDateTimeUtc());
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

void MobileApp::rateCard(int rating)
{
    if (!m_session || !m_session->isActive() || !m_revealed || rating < 1 || rating > 4) return;
    m_session->rate(core::practice::Rating(rating), QDateTime::currentDateTimeUtc());
    m_revealed = false;
    if (!m_session->isActive()) endPractice();
    else emit practiceChanged();
}

void MobileApp::endPractice()
{
    if (!m_session) return;
    using core::practice::Rating;
    const int done = m_session->done();
    m_practiceSummary = done == 0 ? tr("No cards answered.")
                                  : tr("Done for now: %n card(s).", nullptr, done) + QLatin1Char('\n') +
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

void MobileApp::setTextSize(int pct)
{
    pct = qBound(60, pct, 250);
    if (pct == m_settings.look.textSizePct) return;
    m_settings.look.textSizePct = pct;
    saveSettings();
    emit lookChanged();
}

void MobileApp::setMargins(int px)
{
    px = qBound(4, px, 80);
    if (px == m_settings.look.marginPx) return;
    m_settings.look.marginPx = px;
    saveSettings();
    emit lookChanged();
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
    const core::FoundCard f = core::findDictionaryCard({m_definitionsEs, m_dictionaryEn, m_dictionary}, page->selectedText(), languageCode());
    if (!f.found()) return false;
    const Colors c = pageColors();
    m_card = {{QStringLiteral("found"), true},
              {QStringLiteral("title"), f.title.toUpper()},
              {QStringLiteral("html"), core::dictionaryCardHtml(f.card, c.fg, c.muted, c.line)},
              {QStringLiteral("source"), f.source},
              {QStringLiteral("url"), f.fullDefinition.toString(QUrl::FullyEncoded)},
              {QStringLiteral("offer"), f.offerDefinitions},
              {QStringLiteral("word"), f.card.headwords.first().word}};
    emit cardChanged();
    return true;
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

// The end of the page that starts at `p` (its chapter must be laid out), else `p` itself.
int pageEnd(core::BookEngine* engine, const core::Position& p)
{
    const auto cl = engine->layout(p.spine);
    if (!cl) return p.offset;
    const int page = cl->pageForOffset(p.offset);
    return page >= 0 && page < int(cl->pages.size()) ? cl->pages[size_t(page)].end : p.offset;
}

}  // namespace

void MobileApp::toggleBookmark()
{
    if (!m_notes || !m_bookId) return;
    const int end = pageEnd(m_engine, m_position);
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
        m_notes->add(a);
    }
    emit annotationsChanged();
}

bool MobileApp::pageBookmarked() const
{
    if (!m_notes || !m_bookId) return false;
    const int end = pageEnd(m_engine, m_position);
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
    emit jumpRequested(p.spine, p.offset);
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
    emit jumpRequested(ix->toc[size_t(index)].spine, ix->toc[size_t(index)].offset);
}

void MobileApp::search(const QString& query)
{
    m_hits.clear();
    m_searchSummary.clear();
    if (!m_bookId || query.trimmed().size() < 2) {
        m_engine->cancelSearch();
        m_searchId = 0;
        emit searchChanged();
        return;
    }
    m_searchId = m_engine->search(query.trimmed(), core::SearchOptions{});
    m_searchSummary = tr("Searching…");
    emit searchChanged();
}

void MobileApp::goToSearchResult(int index)
{
    if (index < 0 || index >= m_hits.size()) return;
    emit jumpRequested(m_hits[index].spine, m_hits[index].start);
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

void MobileApp::openLink(const QString& url) { QDesktopServices::openUrl(QUrl(url)); }

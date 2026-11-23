#include "MobileApp.h"

#include "BookPage.h"

#include "core/services/BookEngine.h"
#include "core/services/Importer.h"
#include "core/services/Library.h"
#include "core/services/Secrets.h"
#include "core/services/WordLog.h"
#include "core/translate/AiProviders.h"
#include "core/translate/DictionaryDownload.h"
#include "core/translate/OfflineDictionary.h"
#include "core/translate/SavedWords.h"
#include "core/translate/TranslationService.h"

#include <QDesktopServices>
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

// The reading themes of the touch app.
MobileApp::Colors themeColors(const QString& key)
{
    MobileApp::Colors c;   // paper
    if (key == QLatin1String("sepia")) {
        c.bg = QColor(0xF3, 0xE9, 0xD2);
        c.fg = QColor(0x4A, 0x3B, 0x2A);
        c.muted = QColor(0x8A, 0x75, 0x56);
        c.accent = QColor(0x9C, 0x5B, 0x1F);
        c.line = QColor(0xD6, 0xC7, 0xA6);
        c.surface = QColor(0xEA, 0xDF, 0xC4);
        c.sel = QColor(0xE0, 0xB0, 0x60, 0x90);
        c.pop = QColor(0xFB, 0xF4, 0xE4);
    } else if (key == QLatin1String("night")) {
        c.bg = QColor(0x14, 0x14, 0x14);
        c.fg = QColor(0xDA, 0xD4, 0xC8);
        c.muted = QColor(0x8E, 0x87, 0x7B);
        c.accent = QColor(0xE0, 0xA8, 0x4B);
        c.line = QColor(0x2E, 0x2E, 0x2E);
        c.surface = QColor(0x1E, 0x1E, 0x1E);
        c.sel = QColor(0x4A, 0x5A, 0x78, 0xC0);
        c.pop = QColor(0x24, 0x24, 0x24);
    }
    return c;
}

QString safeFileName(QString name)
{
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    name = name.trimmed();
    if (name.isEmpty()) name = QStringLiteral("book.epub");
    if (!name.endsWith(QLatin1String(".epub"), Qt::CaseInsensitive)) name += QStringLiteral(".epub");
    return name;
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
    if (!QStringList{"paper", "sepia", "night"}.contains(m_settings.theme)) m_settings.theme = QStringLiteral("paper");

    m_library = std::make_unique<core::Library>();
    QString err;
    if (!m_library->open(QDir(m_folder).filePath(QStringLiteral("library.sqlite")), &err)) {
        m_library.reset();
        setMessage(tr("The library can't be opened: %1").arg(err));
    }
    m_model = new BookListModel(m_library.get(), this);
    if (m_library) m_words = std::make_unique<core::WordLog>(m_library.get());

    m_engine = new core::BookEngine(this);
    connect(m_engine, &core::BookEngine::opened, this, &MobileApp::onOpened);
    connect(m_engine, &core::BookEngine::openFailed, this, [this](const QString& why) {
        m_openingPath.clear();
        m_openingId = 0;
        setMessage(why);
    });
    connect(m_engine, &core::BookEngine::indexReady, this, &MobileApp::updateLocation);

    m_importer = new core::Importer(this);
    connect(m_importer, &core::Importer::bookRead, this, [this](const core::ImportedBook& b) {
        if (m_library) m_library->addBook(b.path, b.meta, b.coverPng);
        m_model->refresh();
        emit booksChanged();
        setMessage(tr("Added %1.").arg(b.meta.title.isEmpty() ? QFileInfo(b.path).completeBaseName() : b.meta.title));
    });
    connect(m_importer, &core::Importer::failed, this, [this](const QString& path, const QString& why) {
        QFile::remove(path);   // the app's own copy of a file it can't read
        setMessage(why);
    });
    connect(m_importer, &core::Importer::finished, this, [this] {
        m_busy = false;
        emit busyChanged();
    });

    // Translation: Gemini, then what the Word List already says, then the offline dictionary.
    m_net = new QNetworkAccessManager(this);
    m_gemini = new core::GeminiProvider(m_net, this);
    m_gemini->setKey(core::secrets::read(kGeminiKey));
    m_dictionary = new core::OfflineDictionary(this);
    m_dictionary->open(dictionaryFile());
    m_savedWords = new core::SavedWords(this);
    m_savedWords->setDictionaries({m_dictionary});
    m_savedWords->setWordLog(m_words.get());
    m_translator = new core::TranslationService(this);
    m_translator->setCache(m_library.get());
    m_translator->setProviders({m_gemini, m_savedWords, m_dictionary});
    m_translator->setInstant({m_dictionary});
    connect(m_translator, &core::TranslationService::finished, this, &MobileApp::onTranslated);
    connect(m_translator, &core::TranslationService::interim, this, [this](int id, const core::TranslationResult& r) {
        if (id != m_lookupId) return;
        QVariantMap m = m_lookup;
        m[QStringLiteral("translation")] = r.translation;
        m[QStringLiteral("source")] = tr("Offline dictionary · asking online…");
        m_lookup = m;
        emit lookupChanged();
    });

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1500);
    connect(&m_saveTimer, &QTimer::timeout, this, &MobileApp::savePosition);
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

QString MobileApp::dictionaryFile() const { return QDir(m_folder).filePath(QStringLiteral("dictionaries/es-en.sqlite")); }

// ---------------------------------------------------------------- library

void MobileApp::importBook(const QUrl& url)
{
    if (!m_library || !url.isValid()) return;
    // A content:// address (Android's file picker) is read through Qt's Android file support; a file:// one directly.
    const QString source = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const QString name = safeFileName(QFileInfo(source).fileName());
    QDir dir(QDir(m_folder).filePath(QStringLiteral("books")));
    dir.mkpath(QStringLiteral("."));
    QString target = dir.filePath(name);
    for (int i = 2; QFileInfo::exists(target); ++i)
        target = dir.filePath(QStringLiteral("%1 (%2).epub").arg(QFileInfo(name).completeBaseName()).arg(i));
    if (!QFile::copy(source, target)) {
        setMessage(tr("The book couldn't be copied into the app."));
        return;
    }
    m_busy = true;
    emit busyChanged();
    setMessage(tr("Adding %1…").arg(QFileInfo(name).completeBaseName()));
    m_importer->add({target});
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
    m_position = page->position();
    updateLocation();
    m_saveTimer.start();
}

void MobileApp::updateLocation()
{
    if (!m_bookId) return;
    m_chapter = chapterTitleAt(m_position);
    if (const auto ix = m_engine->index()) {
        m_progress = ix->fraction(m_position);
        m_pageText = tr("Page %1 of %2").arg(ix->pageLabel(m_position), ix->lastPageLabel());
    }
    emit locationChanged();
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
        // Every lookup joins the word log, with the place it was seen (as on the desktop).
        if (m_words && m_bookId) {
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
        }
    }
    m_lookup = m;
    emit lookupChanged();
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
    if (key == m_settings.theme || !QStringList{"paper", "sepia", "night"}.contains(key)) return;
    m_settings.theme = key;
    saveSettings();
    emit lookChanged();
}

MobileApp::Colors MobileApp::pageColors() const { return themeColors(m_settings.theme); }

QVariantMap MobileApp::colors() const
{
    const Colors c = pageColors();
    return {{QStringLiteral("bg"), c.bg}, {QStringLiteral("fg"), c.fg}, {QStringLiteral("muted"), c.muted},
            {QStringLiteral("accent"), c.accent}, {QStringLiteral("line"), c.line}, {QStringLiteral("surface"), c.surface},
            {QStringLiteral("pop"), c.pop}, {QStringLiteral("dark"), m_settings.theme == QLatin1String("night")}};
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

void MobileApp::downloadDictionary()
{
    if (m_download && m_download->isRunning()) return;
    if (!m_download) {
        m_download = new core::DictionaryDownload(this);
        connect(m_download, &core::DictionaryDownload::progress, this, [this](qint64 got, qint64 total) {
            m_downloadProgress = total > 0 ? double(got) / double(total) : 0;
            emit downloadChanged();
        });
        connect(m_download, &core::DictionaryDownload::finished, this, [this](const QString& file, const QString& error) {
            m_downloadProgress = -1;
            emit downloadChanged();
            if (!error.isEmpty()) {
                QFile::remove(file);
                setMessage(error);
                return;
            }
            const core::DictionaryInfo info = core::OfflineDictionary::inspect(file);
            if (!info.ok || info.language != QLatin1String("es")) {
                QFile::remove(file);
                setMessage(tr("The downloaded dictionary can't be used."));
                return;
            }
            m_dictionary->close();
            QFile::remove(dictionaryFile());
            QFile::rename(file, dictionaryFile());
            m_dictionary->open(dictionaryFile());
            setMessage(tr("The dictionary is installed."));
            emit translationChanged();
        });
    }
    QDir().mkpath(QFileInfo(dictionaryFile()).absolutePath());
    m_downloadProgress = 0;
    emit downloadChanged();
    m_download->start(core::DictionaryDownload::spanishEnglish(), dictionaryFile() + QStringLiteral(".download"));
}

void MobileApp::cancelDownload()
{
    if (m_download) m_download->cancel();
}

void MobileApp::openLink(const QString& url) { QDesktopServices::openUrl(QUrl(url)); }

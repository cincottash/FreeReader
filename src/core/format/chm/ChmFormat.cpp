#include "ChmFormat.h"

#include "core/format/Convert.h"
#include "core/html/HtmlParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QUrl>
#include <QUuid>

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#ifdef HAVE_CHMLIB
#include <chm_lib.h>
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace core {

bool ChmFormat::canOpen(const QString& path, const QByteArray& head) const
{
    if (head.startsWith("ITSF")) return true;
    return QFileInfo(path).suffix().compare(QLatin1String("chm"), Qt::CaseInsensitive) == 0;
}

#ifndef HAVE_CHMLIB

std::shared_ptr<Book> ChmFormat::open(const QString&, QString* error) const
{
    if (error) *error = QStringLiteral("CHM help files can't be opened on this device yet.");
    return nullptr;
}

#else

namespace {

// The Windows code page of a CHM's language (its LCID), for pages that don't say their encoding.
int codePageOf(quint32 lcid)
{
    switch (lcid & 0x3ff) {   // the primary language
    case 0x11: return 932;    // Japanese
    case 0x04: return (lcid == 0x0404 || lcid == 0x0c04 || lcid == 0x1404) ? 950 : 936;   // Chinese: Big5 or GBK
    case 0x12: return 949;    // Korean
    case 0x19: case 0x22: case 0x23: case 0x02: case 0x2f: return 1251;   // Russian, Ukrainian, Belarusian, Bulgarian, Macedonian
    case 0x05: case 0x15: case 0x0e: case 0x1b: case 0x24: case 0x1a: case 0x18: return 1250;   // Central European
    case 0x08: return 1253;   // Greek
    case 0x1f: return 1254;   // Turkish
    case 0x0d: return 1255;   // Hebrew
    case 0x01: return 1256;   // Arabic
    case 0x25: case 0x26: case 0x27: return 1257;   // Baltic
    case 0x2a: return 1258;   // Vietnamese
    case 0x1e: return 874;    // Thai
    default: return 1252;
    }
}

QString languageOf(quint32 lcid)
{
    static const QHash<int, QString> codes{{0x09, QStringLiteral("en")}, {0x0a, QStringLiteral("es")}, {0x0c, QStringLiteral("fr")},
                                           {0x07, QStringLiteral("de")}, {0x10, QStringLiteral("it")}, {0x16, QStringLiteral("pt")},
                                           {0x11, QStringLiteral("ja")}, {0x04, QStringLiteral("zh")}, {0x12, QStringLiteral("ko")},
                                           {0x19, QStringLiteral("ru")}, {0x13, QStringLiteral("nl")}, {0x15, QStringLiteral("pl")}};
    return codes.value(int(lcid & 0x3ff));
}

int codePageNamed(const QString& name)
{
    const QString n = name.toLower();
    static const QHash<QString, int> names{
        {QStringLiteral("shift_jis"), 932}, {QStringLiteral("shift-jis"), 932}, {QStringLiteral("sjis"), 932}, {QStringLiteral("x-sjis"), 932},
        {QStringLiteral("windows-31j"), 932}, {QStringLiteral("cp932"), 932}, {QStringLiteral("gb2312"), 936}, {QStringLiteral("gbk"), 936},
        {QStringLiteral("cp936"), 936}, {QStringLiteral("big5"), 950}, {QStringLiteral("euc-kr"), 949}, {QStringLiteral("ks_c_5601-1987"), 949},
        {QStringLiteral("koi8-r"), 20866}, {QStringLiteral("iso-8859-1"), 1252}, {QStringLiteral("us-ascii"), 1252}};
    if (names.contains(n)) return names.value(n);
    if (n.startsWith(QLatin1String("windows-"))) return n.mid(8).toInt();
    if (n.startsWith(QLatin1String("cp"))) return n.mid(2).toInt();
    return 0;
}

QString decodeCodePage(const QByteArray& data, int cp, const QString& name)
{
    if (cp == 65001 || name.compare(QLatin1String("utf-8"), Qt::CaseInsensitive) == 0) return QString::fromUtf8(data);
#ifdef Q_OS_WIN
    if (cp > 0 && !data.isEmpty()) {
        const int n = MultiByteToWideChar(UINT(cp), 0, data.constData(), int(data.size()), nullptr, 0);
        if (n > 0) {
            QString out(n, Qt::Uninitialized);
            MultiByteToWideChar(UINT(cp), 0, data.constData(), int(data.size()), reinterpret_cast<wchar_t*>(out.data()), n);
            return out;
        }
    }
#endif
    // Elsewhere through Qt's converters (ICU), by name.
    static const QHash<int, const char*> cpNames{{932, "Shift_JIS"}, {936, "GBK"}, {950, "Big5"}, {949, "EUC-KR"}, {20866, "KOI8-R"}};
    QString n = name;
    if (n.isEmpty()) n = cpNames.contains(cp) ? QString::fromLatin1(cpNames.value(cp)) : QStringLiteral("windows-%1").arg(cp);
    QStringDecoder dec(n.toLatin1().constData());
    if (dec.isValid()) return dec(data);
    return convert::decodeAs(data, n);
}

class ChmBook : public MemoryBook {
public:
    chmFile* chm = nullptr;
    // The copy chmlib reads on Windows: it opens its file so that no other program can read it meanwhile (the
    // library's fingerprint, Search All Books), and takes only 8-bit file names.
    QString copy;   // removed when the book closes
    QHash<QString, QString> names;          // lower-case path -> the path in the file ("/html/a.htm")
    int codePage = 1252;

    ChmBook() : m_thread([this] { work(); }) {}
    ~ChmBook() override
    {
        if (chm) call([this] { chm_close(chm); });
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_wake.notify_one();
        m_thread.join();
        if (!copy.isEmpty()) QFile::remove(copy);
    }

    // Runs `f` on the book's own thread and waits for it. chmlib's locks belong to the thread that takes them,
    // and it leaves some taken: a second thread then waits for ever. So every chmlib call is made by this one.
    void call(const std::function<void()>& f) const
    {
        std::promise<void> done;
        std::future<void> finished = done.get_future();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_jobs.push_back([&] {
                f();
                done.set_value();
            });
        }
        m_wake.notify_one();
        finished.wait();
    }

    static QByteArray read(chmFile* h, const QByteArray& path, qulonglong limit)
    {
        chmUnitInfo ui;
        if (!h || chm_resolve_object(h, path.constData(), &ui) != CHM_RESOLVE_SUCCESS || ui.length > limit) return {};
        QByteArray data(qsizetype(ui.length), Qt::Uninitialized);
        const LONGINT64 n = ui.length ? chm_retrieve_object(h, &ui, reinterpret_cast<unsigned char*>(data.data()), 0, LONGINT64(ui.length)) : 0;
        if (n < 0) return {};
        data.truncate(qsizetype(n));
        return data;
    }

    QByteArray raw(const QString& path, bool* ok) const
    {
        const QString real = names.value(path.toLower());
        QByteArray data;
        bool found = false;
        if (!real.isEmpty())
            call([&] {
                chmUnitInfo ui;
                found = chm && chm_resolve_object(chm, real.toUtf8().constData(), &ui) == CHM_RESOLVE_SUCCESS;
                if (found) data = read(chm, real.toUtf8(), 512u << 20);
            });
        if (ok) *ok = found;
        return data;
    }

    // A page as UTF-8: in the encoding it declares, else in the file's language's.
    QByteArray page(const QByteArray& data) const
    {
        if (data.startsWith("\xEF\xBB\xBF") || data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF")) return convert::decodeText(data).toUtf8();
        const QString head = QString::fromLatin1(data.left(4096));
        static const QRegularExpression charset(QStringLiteral("charset\\s*=\\s*[\"']?([A-Za-z0-9_.:-]+)"), QRegularExpression::CaseInsensitiveOption);
        const auto m = charset.match(head);
        const QString declared = m.hasMatch() ? m.captured(1) : QString();
        const int cp = declared.isEmpty() ? codePage : codePageNamed(declared);
        QString text = decodeCodePage(data, cp, declared);
        static const QRegularExpression decl(QStringLiteral("(charset\\s*=\\s*[\"']?|encoding\\s*=\\s*[\"'])[A-Za-z0-9_.:-]+"),
                                             QRegularExpression::CaseInsensitiveOption);
        const qsizetype headEnd = qMin<qsizetype>(text.size(), 4096);
        QString h = text.left(headEnd);
        h.replace(decl, QStringLiteral("\\1utf-8"));
        return (h + text.mid(headEnd)).toUtf8();
    }

    QByteArray resource(const QString& path, bool* ok) const override
    {
        if (files.contains(path)) return MemoryBook::resource(path, ok);
        const QByteArray data = raw(path, ok);
        const QString s = QFileInfo(path).suffix().toLower();
        if (!data.isEmpty() && (s == QLatin1String("htm") || s == QLatin1String("html"))) return page(data);
        return data;
    }
    bool hasResource(const QString& path) const override { return files.contains(path) || names.contains(path.toLower()); }

private:
    void work()
    {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
                if (m_jobs.empty()) return;   // stopping, nothing left to do
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            job();
        }
    }

    mutable std::mutex m_mutex;
    mutable std::condition_variable m_wake;
    mutable std::deque<std::function<void()>> m_jobs;
    bool m_stop = false;
    std::thread m_thread;   // last: started once everything above exists
};

int collect(chmFile*, chmUnitInfo* ui, void* context)
{
    auto* book = static_cast<ChmBook*>(context);
    const QString p = QString::fromUtf8(ui->path);
    if (p.startsWith(QLatin1Char('/')) && !p.endsWith(QLatin1Char('/')) && !p.startsWith(QLatin1String("/#")) && !p.startsWith(QLatin1String("/$")))
        book->names.insert(p.mid(1).toLower(), p);
    return CHM_ENUMERATOR_CONTINUE;
}


// A link in the contents or a page ("Html\Page 1.htm#top", "ms-its:book.chm::/a.htm") as a path in the book.
QString cleanLink(QString l)
{
    l = l.trimmed();
    const qsizetype its = l.indexOf(QLatin1String("::"));
    if (its >= 0) l = l.mid(its + 2);
    l.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (l.startsWith(QLatin1Char('/'))) l.remove(0, 1);
    const qsizetype hash = l.indexOf(QLatin1Char('#'));
    const QString frag = hash >= 0 ? l.mid(hash) : QString();
    QString p = QUrl::fromPercentEncoding((hash >= 0 ? l.left(hash) : l).toUtf8());
    p = QDir::cleanPath(p);
    return p + frag;
}

}  // namespace

std::shared_ptr<Book> ChmFormat::open(const QString& path, QString* error) const
{
    auto book = std::make_shared<ChmBook>();
    book->filePath = path;
    QByteArray name = QFile::encodeName(QDir::toNativeSeparators(path));
#ifdef Q_OS_WIN
    // chmlib reads a copy (see ChmBook::copy), in the temporary folder, removed when the book closes.
    {
        QFile src(path);
        if (!src.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("The file can't be read: %1").arg(src.errorString());
            return nullptr;
        }
        // A plain file (a QTemporaryFile keeps a handle Windows counts against chmlib's open).
        book->copy = QDir::temp().filePath(QStringLiteral("freereader-chm-%1.chm").arg(QUuid::createUuid().toString(QUuid::Id128)));
        QFile dst(book->copy);
        bool written = dst.open(QIODevice::WriteOnly);
        char buf[1 << 16];
        qint64 n;
        while (written && (n = src.read(buf, sizeof buf)) > 0) written = dst.write(buf, n) == n;
        dst.close();
        if (!written) {
            if (error) *error = QStringLiteral("There's no room in the temporary folder to open this file.");
            return nullptr;   // the destructor removes the part written
        }
        name = QDir::toNativeSeparators(book->copy).toLocal8Bit();
    }
#endif
    QByteArray sys;
    book->call([&] {
        book->chm = chm_open(name.constData());
        if (!book->chm) return;
        chm_enumerate(book->chm, CHM_ENUMERATE_NORMAL | CHM_ENUMERATE_FILES, collect, book.get());
        sys = ChmBook::read(book->chm, "/#SYSTEM", 16u << 20);
    });
    if (!book->chm) {
        if (error) *error = QStringLiteral("This help file is damaged, or isn't a CHM file.");
        return nullptr;
    }

    // #SYSTEM: the contents file, the first page, the title and the language.
    QString hhc, home;
    QByteArray title;
    quint32 lcid = 0;
    for (qsizetype at = 4; at + 4 <= sys.size();) {
        const int code = quint8(sys[at]) | quint8(sys[at + 1]) << 8;
        const int len = quint8(sys[at + 2]) | quint8(sys[at + 3]) << 8;
        const QByteArray v = sys.mid(at + 4, len);
        at += 4 + len;
        const QByteArray s = v.left(v.indexOf('\0') >= 0 ? v.indexOf('\0') : v.size());
        if (code == 0) hhc = QString::fromLatin1(s);
        else if (code == 2) home = QString::fromLatin1(s);
        else if (code == 3) title = s;
        else if (code == 4 && v.size() >= 4) lcid = quint8(v[0]) | quint8(v[1]) << 8 | quint8(v[2]) << 16 | quint32(quint8(v[3])) << 24;
    }
    if (lcid) {
        book->codePage = codePageOf(lcid);
        book->metadata.language = languageOf(lcid);
    }
    if (!title.isEmpty()) book->metadata.title = decodeCodePage(title, book->codePage, {}).simplified();
    if (hhc.isEmpty())
        for (auto it = book->names.cbegin(); it != book->names.cend(); ++it)
            if (it.key().endsWith(QLatin1String(".hhc"))) {
                hhc = it.key();
                break;
            }

    // The contents (a sitemap page: nested lists of <object type="text/sitemap"> with Name and Local).
    QStringList order;
    QSet<QString> inSpine;
    auto addSpine = [&](const QString& link) {
        const QString file = link.section(QLatin1Char('#'), 0, 0);
        const QString key = file.toLower();
        const QString s = QFileInfo(file).suffix().toLower();
        if (file.isEmpty() || inSpine.contains(key) || !book->names.contains(key) || (s != QLatin1String("htm") && s != QLatin1String("html"))) return false;
        inSpine.insert(key);
        order << file;
        return true;
    };
    struct Entry {
        QString title, link;
        int depth;
    };
    std::vector<Entry> entries;
    if (!hhc.isEmpty()) {
        bool ok = false;
        const QByteArray data = book->raw(cleanLink(hhc), &ok);
        if (ok) {
            const auto doc = dom::parse(book->page(data));
            std::function<void(const dom::Node*, int)> walk = [&](const dom::Node* n, int depth) {
                for (const auto& c : n->children) {
                    if (!c->isElement()) continue;
                    if (c->is("object") && c->attr("type").compare(QLatin1String("text/sitemap"), Qt::CaseInsensitive) == 0) {
                        QString title, local;
                        std::function<void(const dom::Node*)> params = [&](const dom::Node* o) {
                            for (const auto& p : o->children) {
                                if (!p->isElement()) continue;
                                if (p->is("param")) {
                                    const QString k = p->attr("name").toLower();
                                    if (k == QLatin1String("name") && title.isEmpty()) title = p->attr("value").simplified();
                                    else if (k == QLatin1String("local") && local.isEmpty()) local = p->attr("value");
                                }
                                params(p.get());
                            }
                        };
                        params(c.get());
                        if (!title.isEmpty()) entries.push_back({title, local.isEmpty() ? QString() : cleanLink(local), depth});
                        continue;
                    }
                    walk(c.get(), (c->is("ul") || c->is("ol")) ? depth + 1 : depth);
                }
            };
            walk(doc->root.get(), -1);
        }
    }
    if (!home.isEmpty()) addSpine(cleanLink(home));
    for (const Entry& e : entries) addSpine(e.link);
    // Pages the contents leave out, after them in natural order (most help files list every page; some none).
    QStringList rest;
    for (auto it = book->names.cbegin(); it != book->names.cend(); ++it) {
        const QString s = QFileInfo(it.key()).suffix();
        if ((s == QLatin1String("htm") || s == QLatin1String("html")) && !inSpine.contains(it.key())) rest << it.value().mid(1);
    }
    std::sort(rest.begin(), rest.end(), convert::naturalLess);
    if (order.size() < 400)
        for (const QString& r : rest) addSpine(r);
    if (order.isEmpty()) {
        if (error) *error = QStringLiteral("This help file has no pages to show.");
        return nullptr;
    }
    for (const QString& p : order) {
        book->spine.push_back({p, QString(), true});
    }
    int minDepth = 99;
    for (const Entry& e : entries) minDepth = qMin(minDepth, qMax(0, e.depth));
    for (const Entry& e : entries) {
        if (e.link.isEmpty() || !inSpine.contains(e.link.section(QLatin1Char('#'), 0, 0).toLower())) continue;
        // The spine's spelling of the file (the contents may differ in case).
        QString file = e.link.section(QLatin1Char('#'), 0, 0);
        for (const QString& p : order)
            if (p.compare(file, Qt::CaseInsensitive) == 0) file = p;
        const QString frag = e.link.contains(QLatin1Char('#')) ? QLatin1Char('#') + e.link.section(QLatin1Char('#'), 1) : QString();
        book->addTocEntry(e.title, file + frag, qMax(0, e.depth) - minDepth);
    }
    if (book->metadata.title.isEmpty()) book->metadata.title = entries.empty() ? QFileInfo(path).completeBaseName() : entries.front().title;
    if (book->metadata.language.isEmpty()) {
        QString sample;
        for (int i = 0; i < order.size() && i < 10 && sample.size() < 20000; ++i)
            sample += dom::parse(book->resource(order[i], nullptr))->root->textContent();
        book->metadata.language = convert::guessLanguage(sample);
    }
    return book;
}

#endif  // HAVE_CHMLIB

}  // namespace core

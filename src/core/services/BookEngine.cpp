#include "BookEngine.h"

#include "core/format/FormatPlugin.h"
#include "core/html/HtmlParser.h"
#include "core/layout/Fonts.h"

#include <QFutureWatcher>
#include <QtConcurrent>

namespace core {

namespace {

constexpr int kCacheSize = 6;

bool isBlockName(const QString& n)
{
    static const QSet<QString> s = {"p", "div", "li", "aside", "section", "dd", "dt", "blockquote", "td", "body", "article", "footer"};
    return s.contains(n);
}

}  // namespace

BookEngine::BookEngine(QObject* parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(qBound(2, QThread::idealThreadCount() - 1, 4));
}

BookEngine::~BookEngine()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void BookEngine::open(const QString& path)
{
    close();
    const int token = ++m_openToken;
    m_pool.start([this, path, token] {
        QString error;
        std::shared_ptr<Book> book = FormatRegistry::instance().open(path, &error);
        QMetaObject::invokeMethod(this, [this, book, error, token] {
            if (token != m_openToken) return;
            if (!book) {
                emit openFailed(error);
                return;
            }
            m_book = book;
            m_fonts = std::make_shared<layout::FontResolver>();
            emit opened();
            // The index (chapter lengths, ids, table of contents positions) is built in the background.
            const int t = m_openToken;
            m_pool.start([this, book, t] {
                auto ix = buildIndex(*book);
                QMetaObject::invokeMethod(this, [this, ix, t] {
                    if (t != m_openToken) return;
                    m_index = ix;
                    emit indexReady();
                }, Qt::QueuedConnection);
            });
        }, Qt::QueuedConnection);
    });
}

void BookEngine::close()
{
    ++m_openToken;
    ++m_generation;
    m_pool.clear();
    m_pool.waitForDone();
    m_cache.clear();
    m_recent.clear();
    m_pending.clear();
    m_index.reset();
    m_book.reset();
    m_fonts.reset();   // removes the book's fonts from the application
}

void BookEngine::setParams(const layout::LayoutParams& p)
{
    if (p == m_params) return;
    m_params = p;
    ++m_generation;
    m_cache.clear();
    m_recent.clear();
    m_pending.clear();
}

void BookEngine::setBodyFont(const QStringList& families)
{
    if (m_fonts) m_fonts->setBodyFamilies(families);
    ++m_generation;
    m_cache.clear();
    m_recent.clear();
    m_pending.clear();
}

std::shared_ptr<const layout::ChapterLayout> BookEngine::layout(int spine)
{
    const auto it = m_cache.constFind(spine);
    if (it != m_cache.cend()) {
        m_recent.removeAll(spine);
        m_recent.prepend(spine);
        return *it;
    }
    request(spine);
    return nullptr;
}

void BookEngine::request(int spine)
{
    if (!m_book || spine < 0 || spine >= int(m_book->spine.size())) return;
    if (m_cache.contains(spine) || m_pending.contains(spine)) return;
    m_pending.insert(spine);
    const int gen = m_generation;
    const auto book = m_book;
    const auto fonts = m_fonts;
    const layout::LayoutParams params = m_params;
    m_pool.start([this, book, fonts, params, spine, gen] {
        layout::FlowOptions opt;
        opt.pxPerCssPx = params.pxPerCssPx;
        opt.fonts = fonts.get();
        std::shared_ptr<const layout::ChapterFlow> flow = layout::buildFlow(*book, spine, opt);
        std::shared_ptr<const layout::ChapterLayout> cl = layout::layoutChapter(flow, *book, spine, params);
        QMetaObject::invokeMethod(this, [this, spine, gen, cl] { store(spine, gen, cl); }, Qt::QueuedConnection);
    });
}

void BookEngine::store(int spine, int generation, std::shared_ptr<const layout::ChapterLayout> cl)
{
    if (generation != m_generation) return;
    m_pending.remove(spine);
    m_cache.insert(spine, std::move(cl));
    m_recent.removeAll(spine);
    m_recent.prepend(spine);
    while (m_recent.size() > kCacheSize) m_cache.remove(m_recent.takeLast());
    emit layoutReady(spine);
}

std::shared_ptr<const layout::ChapterLayout> BookEngine::layoutNow(int spine)
{
    if (auto cl = layout(spine)) return cl;
    if (!m_book || spine < 0 || spine >= int(m_book->spine.size())) return nullptr;
    layout::FlowOptions opt;
    opt.pxPerCssPx = m_params.pxPerCssPx;
    opt.fonts = m_fonts.get();
    auto flow = layout::buildFlow(*m_book, spine, opt);
    std::shared_ptr<const layout::ChapterLayout> cl = layout::layoutChapter(flow, *m_book, spine, m_params);
    m_cache.insert(spine, cl);
    m_recent.prepend(spine);
    return cl;
}

std::shared_ptr<const layout::ChapterFlow> BookEngine::flowFor(int spine)
{
    const auto it = m_cache.constFind(spine);
    if (it != m_cache.cend()) return (*it)->flow;
    if (!m_book || spine < 0 || spine >= int(m_book->spine.size())) return nullptr;
    layout::FlowOptions opt;   // positions don't depend on fonts
    return layout::buildFlow(*m_book, spine, opt);
}

QString BookEngine::cfiFor(const Position& p)
{
    const auto flow = flowFor(p.spine);
    return flow ? makeCfi(p.spine, *flow, p.offset) : QString();
}

bool BookEngine::positionForCfi(const QString& cfi, Position* out)
{
    int spine = 0, raw = 0;
    QString path;
    if (!parseCfi(cfi, &spine, &path, &raw) || !m_book || spine >= int(m_book->spine.size())) return false;
    const auto flow = flowFor(spine);
    if (!flow) return false;
    const int off = resolveCfiPath(*flow, path, raw);
    if (off < 0) return false;
    *out = {spine, off};
    return true;
}

NoteText BookEngine::noteText(const QString& href) const
{
    NoteText out;
    if (!m_book) return out;
    const qsizetype hash = href.indexOf(QLatin1Char('#'));
    if (hash < 0) return out;
    bool ok = false;
    const QByteArray data = m_book->resource(href.left(hash), &ok);
    if (!ok) return out;
    const auto doc = dom::parse(data);
    const dom::Node* n = doc->byId(href.mid(hash + 1));
    if (!n) return out;
    for (const dom::Node* a = n; a; a = a->parent) {
        if (!a->isElement()) continue;
        const QString t = a->attr("epub:type");
        if (t.contains(QLatin1String("footnote")) || t.contains(QLatin1String("endnote")) || t.contains(QLatin1String("rearnote"))
            || a->attr("role") == QLatin1String("doc-footnote") || a->attr("role") == QLatin1String("doc-endnote")) {
            out.isNote = true;
            n = a;
            break;
        }
    }
    // An anchor inside a paragraph: show the paragraph.
    while (n && n->parent && !isBlockName(n->name)) n = n->parent;
    if (!n || n->is("body")) return out;
    QStringList paras;
    std::function<void(const dom::Node*)> collect = [&](const dom::Node* e) {
        bool hasBlockChild = false;
        for (const auto& c : e->children)
            if (c->isElement() && isBlockName(c->name)) hasBlockChild = true;
        if (!hasBlockChild) {
            const QString t = e->textContent().simplified();
            if (!t.isEmpty()) paras << t;
            return;
        }
        for (const auto& c : e->children)
            if (c->isElement()) collect(c.get());
    };
    collect(n);
    out.text = paras.join(QStringLiteral("\n\n"));
    if (out.text.size() > 4000) out.text = out.text.left(4000) + QStringLiteral("…");
    return out;
}

QByteArray BookEngine::resource(const QString& path) const { return m_book ? m_book->resource(path) : QByteArray(); }

}  // namespace core

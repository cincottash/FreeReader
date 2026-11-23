#include "BookPage.h"

#include "MobileApp.h"

#include "core/layout/PageRenderer.h"
#include "core/services/BookEngine.h"

#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QTextBoundaryFinder>

#include <climits>
#include <cmath>
#include <numbers>

namespace {
constexpr int kEndOfChapter = INT_MAX;   // "the last page of the chapter", resolved when its layout is ready
}

BookPage::BookPage(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    m_blink.setInterval(60);
    connect(&m_blink, &QTimer::timeout, this, [this] {
        if (!hasSelection() || !isVisible()) m_blink.stop();
        update();
    });
    setOpaquePainting(true);
    setAntialiasing(false);
}

void BookPage::setApp(MobileApp* app)
{
    if (m_app == app) return;
    if (m_app) disconnect(m_app, nullptr, this, nullptr);
    m_app = app;
    if (m_app) {
        connect(m_app->engine(), &core::BookEngine::layoutReady, this, &BookPage::onLayoutReady);
        connect(m_app, &MobileApp::lookChanged, this, [this] {
            applyParams();
            render();
        });
        connect(m_app, &MobileApp::annotationsChanged, this, &BookPage::render);
        connect(m_app, &MobileApp::jumpRequested, this, [this](int spine, int offset) { moveTo({spine, offset}); });
        connect(m_app, &MobileApp::selectRequested, this, [this](int spine, int start, int end) {
            if (spine != m_anchor.spine) return;
            m_selSpine = spine;
            m_selStart = m_selAnchorStart = start;
            m_selEnd = m_selAnchorEnd = end;
            selectionMoved();
            emit selectionChanged();
        });
        m_anchor = m_app->startPosition();
        applyParams();
    }
    emit appChanged();
}

std::shared_ptr<const layout::ChapterLayout> BookPage::current() const
{
    return m_app ? m_app->engine()->layout(m_anchor.spine) : nullptr;
}

bool BookPage::isReady() const { return m_app && m_app->engine()->isOpen() && current() != nullptr; }

QRectF BookPage::pageRect() const
{
    return QRectF(m_marginX, m_top, std::floor(width() - 2 * m_marginX), std::floor(height() - m_top - m_bottom));
}

void BookPage::applyParams()
{
    if (!m_app || width() < 50 || height() < 50) return;
    const Appearance a = m_app->appearance();
    m_marginX = float(a.marginPx);
    layout::LayoutParams p;
    p.columnWidth = float(pageRect().width());
    p.pageHeight = float(pageRect().height());
    p.pxPerCssPx = 1.25f * float(a.textSizePct) / 100.f;
    p.lineSpacing = float(a.lineSpacingPct) / 100.f;
    p.justify = a.justify;
    p.hyphenate = a.hyphenate;
    p.fontKey = a.font;
    p.devicePixelRatio = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    m_app->engine()->setBodyFont(a.font.isEmpty() ? QStringList() : QStringList{a.font});
    m_app->engine()->setParams(p);
    if (m_app->engine()->isOpen()) m_app->engine()->request(m_anchor.spine);
}

void BookPage::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        applyParams();
        render();
    }
}

void BookPage::onLayoutReady(int spine)
{
    if (spine != m_anchor.spine) return;
    const auto cl = current();
    if (!cl) return;
    m_shown = cl;
    if (m_anchor.offset == kEndOfChapter && !cl->pages.empty()) {
        m_anchor.offset = cl->pages.back().start;
        emit positionChanged();
    }
    m_app->engine()->request(spine + 1);
    m_app->engine()->request(spine - 1);
    render();
    emit readyChanged();
}

void BookPage::setPosition(const core::Position& p) { moveTo(p); }

void BookPage::moveTo(const core::Position& p)
{
    if (!m_app || !m_app->engine()->book()) return;
    const int count = int(m_app->engine()->book()->spine.size());
    m_anchor = p;
    m_anchor.spine = qBound(0, m_anchor.spine, qMax(0, count - 1));
    if (m_anchor.offset < 0) m_anchor.offset = 0;
    if (m_selSpine >= 0) clearSelection();
    if (current()) onLayoutReady(m_anchor.spine);
    else m_app->engine()->request(m_anchor.spine);
    render();
    if (m_anchor.offset != kEndOfChapter) emit positionChanged();
}

bool BookPage::nextPage()
{
    const auto cl = current();
    if (!cl) return false;
    const int page = cl->pageForOffset(m_anchor.offset) + 1;
    if (page < int(cl->pages.size())) moveTo({m_anchor.spine, cl->pages[size_t(page)].start});
    else if (m_anchor.spine + 1 < int(m_app->engine()->book()->spine.size())) moveTo({m_anchor.spine + 1, 0});
    else return false;
    return true;
}

bool BookPage::previousPage()
{
    const auto cl = current();
    if (!cl) return false;
    const int page = cl->pageForOffset(m_anchor.offset);
    if (page > 0) moveTo({m_anchor.spine, cl->pages[size_t(page - 1)].start});
    else if (m_anchor.spine > 0) moveTo({m_anchor.spine - 1, kEndOfChapter});
    else return false;
    return true;
}

int BookPage::offsetAt(qreal x, qreal y, int* charOffset) const
{
    const auto cl = current();
    if (charOffset) *charOffset = -1;
    if (!cl || cl->pages.empty()) return -1;
    const int page = cl->pageForOffset(m_anchor.offset);
    const QRectF r = pageRect();
    const layout::Hit h = layout::hitTest(*cl, page, QPointF(x, y) - r.topLeft());
    if (charOffset) *charOffset = h.charOffset;
    return h.offset;
}

bool BookPage::wordAt(int at, int* start, int* end) const
{
    const auto cl = current();
    if (!cl || at < 0) return false;
    for (const layout::Block& b : cl->flow->blocks) {
        if (b.kind != layout::Block::Text || at < b.start || at >= b.start + b.length) continue;
        auto isWordChar = [&](int i) { return i >= 0 && i < int(b.text.size()) && (b.text[i].isLetterOrNumber() || b.text[i] == QChar(0x00AD)); };
        int i = at - b.start;
        if (!isWordChar(i)) {
            if (isWordChar(i - 1)) --i;
            else return false;
        }
        QTextBoundaryFinder f(QTextBoundaryFinder::Word, b.text);
        f.setPosition(i);
        int s = f.isAtBoundary() && (f.boundaryReasons() & QTextBoundaryFinder::StartOfItem) ? i : int(f.toPreviousBoundary());
        f.setPosition(i);
        int e = int(f.toNextBoundary());
        if (s < 0) s = 0;
        if (e < 0) e = int(b.text.size());
        while (e > s && !isWordChar(e - 1)) --e;
        while (s < e && !isWordChar(s)) ++s;
        if (s >= e) return false;
        *start = b.start + s;
        *end = b.start + e;
        return true;
    }
    return false;
}

bool BookPage::selectWordAt(qreal x, qreal y)
{
    int ch = -1;
    offsetAt(x, y, &ch);
    int s = 0, e = 0;
    if (!wordAt(ch, &s, &e)) {
        clearSelection();
        return false;
    }
    m_selSpine = m_anchor.spine;
    m_selStart = m_selAnchorStart = s;
    m_selEnd = m_selAnchorEnd = e;
    selectionMoved();
    emit selectionChanged();
    return true;
}

void BookPage::extendSelectionTo(qreal x, qreal y)
{
    if (m_selSpine != m_anchor.spine) return;
    int ch = -1;
    offsetAt(x, y, &ch);
    int s = 0, e = 0;
    if (!wordAt(ch, &s, &e)) return;
    m_selStart = qMin(s, m_selAnchorStart);
    m_selEnd = qMax(e, m_selAnchorEnd);
    selectionMoved();
    emit selectionChanged();
}

void BookPage::selectSentence()
{
    const auto cl = current();
    if (!cl || !hasSelection()) return;
    for (const layout::Block& b : cl->flow->blocks) {
        if (b.kind != layout::Block::Text || m_selStart < b.start || m_selStart >= b.start + b.length) continue;
        QTextBoundaryFinder f(QTextBoundaryFinder::Sentence, b.text);
        f.setPosition(m_selStart - b.start);
        int s = f.isAtBoundary() ? m_selStart - b.start : int(f.toPreviousBoundary());
        f.setPosition(qMin(int(b.text.size()), qMax(m_selEnd - b.start, s + 1)));
        int e = f.isAtBoundary() ? f.position() : int(f.toNextBoundary());
        if (s < 0) s = 0;
        if (e < 0) e = int(b.text.size());
        while (e > s && b.text[e - 1].isSpace()) --e;
        m_selStart = b.start + s;
        m_selEnd = b.start + e;
        selectionMoved();
        emit selectionChanged();
        return;
    }
}

void BookPage::clearSelection()
{
    if (m_selSpine < 0 && m_selStart == m_selEnd) return;
    m_selSpine = -1;
    m_selStart = m_selEnd = 0;
    selectionMoved();
    emit selectionChanged();
}

QString BookPage::selectedText() const
{
    const auto cl = current();
    if (!cl || m_selSpine != m_anchor.spine || !hasSelection()) return {};
    return cl->flow->textRange(m_selStart, m_selEnd).simplified();
}

QString BookPage::selectionSentence() const
{
    const auto cl = current();
    if (!cl || m_selSpine != m_anchor.spine || !hasSelection()) return {};
    for (const layout::Block& b : cl->flow->blocks) {
        if (b.kind != layout::Block::Text || m_selStart < b.start || m_selStart >= b.start + b.length) continue;
        QTextBoundaryFinder f(QTextBoundaryFinder::Sentence, b.text);
        f.setPosition(m_selStart - b.start);
        int s = f.isAtBoundary() ? m_selStart - b.start : int(f.toPreviousBoundary());
        if (s < 0) s = 0;
        f.setPosition(qMin(int(b.text.size()), qMax(m_selEnd - b.start, s + 1)));
        int e = f.isAtBoundary() ? f.position() : int(f.toNextBoundary());
        if (e < 0) e = int(b.text.size());
        return QString(b.text.mid(s, e - s)).simplified();
    }
    return {};
}

void BookPage::render()
{
    if (width() < 1 || height() < 1) return;
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const MobileApp::Colors c = m_app ? m_app->pageColors() : MobileApp::Colors{};
    QImage img(QSize(qCeil(width() * dpr), qCeil(height() * dpr)), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(c.bg);
    auto cl = current();
    if (!cl && m_shown && m_shown->spine == m_anchor.spine) cl = m_shown;
    if (cl && !cl->pages.empty()) {
        QPainter p(&img);
        const layout::RenderColors colors{c.fg, c.muted, c.accent, c.line, c.surface};
        const std::vector<layout::Overlay> overlays = m_app ? m_app->highlightsIn(m_anchor.spine) : std::vector<layout::Overlay>();
        const int page = cl->pageForOffset(m_anchor.offset);
        layout::paintPage(p, *cl, page, pageRect().topLeft(), colors, overlays);
        // Word bookmarks: a small ribbon in the accent color over the word's first letter.
        p.setRenderHint(QPainter::Antialiasing);
        for (const auto& [ws, we] : m_app ? m_app->wordBookmarksIn(m_anchor.spine) : std::vector<std::pair<int, int>>()) {
            const std::vector<QRectF> rects = layout::rangeRects(*cl, page, ws, qMin(we, ws + 1));
            if (rects.empty()) continue;
            const QRectF r = rects.front().translated(pageRect().topLeft());
            const qreal w = qBound(6.0, r.height() * 0.32, 12.0), h = w * 1.5, x = r.left(), y = r.top() - h * 0.45;
            QPainterPath ribbon;
            ribbon.moveTo(x, y);
            ribbon.lineTo(x + w, y);
            ribbon.lineTo(x + w, y + h);
            ribbon.lineTo(x + w / 2, y + h - w * 0.45);
            ribbon.lineTo(x, y + h);
            ribbon.closeSubpath();
            p.fillPath(ribbon, c.accent);
        }
    }
    m_image = img;
    update();
}

void BookPage::paint(QPainter* painter)
{
    if (m_image.isNull()) return;
    painter->drawImage(QRectF(0, 0, width(), height()), m_image);
    // The selection over the page: multiplied on a light page and screened on a dark one, so the text stays as
    // it is (as if it were under the words). It blinks gently: it's temporary, unlike a highlight.
    const auto cl = current();
    if (!cl || cl->pages.empty() || m_selSpine != m_anchor.spine || !hasSelection()) return;
    const MobileApp::Colors c = m_app ? m_app->pageColors() : MobileApp::Colors{};
    QColor sel = c.sel;
    sel.setAlphaF(float(qMin(1.0, sel.alphaF() * 1.6) * selectionStrength()));
    painter->setCompositionMode(c.dark ? QPainter::CompositionMode_Screen : QPainter::CompositionMode_Multiply);
    const QPointF origin = pageRect().topLeft();
    for (const QRectF& r : layout::rangeRects(*cl, cl->pageForOffset(m_anchor.offset), m_selStart, m_selEnd))
        painter->fillRect(r.translated(origin), sel);
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
}

void BookPage::setSteady(bool on)
{
    if (m_steady == on) return;
    m_steady = on;
    selectionMoved();
    emit steadyChanged();
}

double BookPage::selectionStrength() const
{
    if (m_steady || !m_blink.isActive()) return 1.0;
    // Full, fading to a fifth, and back, once a second.
    const double phase = 0.5 + 0.5 * std::cos(2.0 * std::numbers::pi * double(m_blinkClock.elapsed() % 1000) / 1000.0);
    return 0.2 + 0.8 * phase;
}

void BookPage::selectionMoved()
{
    // Blinking starts again from full each time the selection changes; it stops while a finger holds it.
    if (hasSelection() && !m_steady) {
        m_blinkClock.start();
        if (!m_blink.isActive()) m_blink.start();
    } else {
        m_blink.stop();
    }
    update();
}

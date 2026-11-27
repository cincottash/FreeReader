#include "BookPage.h"

#include "MobileApp.h"

#include "core/layout/PageRenderer.h"
#include "core/layout/PageTurn.h"
#include "core/services/BookEngine.h"

#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QTextBoundaryFinder>
#include <QVariantAnimation>

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
    m_turn = new QVariantAnimation(this);
    m_turn->setStartValue(0.0);
    m_turn->setEndValue(1.0);
    connect(m_turn, &QVariantAnimation::valueChanged, this, [this] { update(); });
    connect(m_turn, &QVariantAnimation::finished, this, [this] {
        m_fromImage = m_toImage = QImage();
        update();
        emit animatingChanged();
    });
}

bool BookPage::isAnimating() const { return m_turn->state() == QAbstractAnimation::Running || !m_toImage.isNull(); }

void BookPage::setTurnProgressForTests(double t)
{
    if (m_toImage.isNull()) return;
    m_turn->pause();
    m_turn->setCurrentTime(int(t * m_turn->duration()));
    update();
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

BookPage::Geometry BookPage::geometryFor(const QSizeF& size) const
{
    // The desktop's rules (PageView::geometryFor), with a phone's top and bottom margins.
    Geometry g;
    const Appearance a = m_app ? m_app->appearance() : Appearance();
    const float bodyPx = 20.f * float(a.textSizePct) / 100.f;
    const float minSide = float(a.marginPx), gap = qMax(40.f, 2.f * bodyPx + float(a.marginPx) / 2.f);
    const float availW = float(size.width()) - 2 * minSide;
    const float maxCol = qMax(200.f, float(a.pageWidthEm) * bodyPx);
    int cols = 1;
    if (a.columns == QLatin1String("two") && availW >= 2 * 160.f + gap) cols = 2;
    // Automatic: two pages only when each can be at least 90 % of the chosen page width.
    else if (a.columns == QLatin1String("auto") && availW >= 2 * qMin(maxCol, 30.f * bodyPx) * 0.9f + gap + 64.f) cols = 2;
    g.columns = cols;
    g.gap = gap;
    g.columnWidth = qMax(120.f, std::floor(qMin(maxCol, (availW - gap * float(cols - 1)) / float(cols))));
    const float used = g.columnWidth * float(cols) + gap * float(cols - 1);
    g.left = std::floor((float(size.width()) - used) / 2.f);
    g.top = 14;
    g.pageHeight = std::floor(qMax(120.f, float(size.height()) - g.top - 30.f));
    return g;
}

QRectF BookPage::columnRect(int i) const
{
    return QRectF(m_geo.left + float(i) * (m_geo.columnWidth + m_geo.gap), m_geo.top, m_geo.columnWidth, m_geo.pageHeight);
}

int BookPage::columnAt(const QPointF& pos) const
{
    for (int i = 0; i < m_geo.columns; ++i)
        if (columnRect(i).adjusted(-m_geo.gap / 2, -m_geo.top, m_geo.gap / 2, 30).contains(pos)) return i;
    // Outside every column (the margins): the nearest one.
    return pos.x() < m_geo.left + m_geo.columnWidth + m_geo.gap / 2 ? 0 : m_geo.columns - 1;
}

int BookPage::firstPage(const layout::ChapterLayout& cl) const
{
    const int page = cl.pageForOffset(m_anchor.offset);
    const int c = qMax(1, m_geo.columns);
    return page - page % c;
}

core::Position BookPage::screenStart() const
{
    const auto cl = current();
    if (!cl || cl->pages.empty()) return m_anchor;
    return {m_anchor.spine, cl->pages[size_t(firstPage(*cl))].start};
}

core::Position BookPage::screenEnd() const
{
    const auto cl = current();
    if (!cl || cl->pages.empty()) return m_anchor;
    const int last = qMin(int(cl->pages.size()) - 1, firstPage(*cl) + m_geo.columns - 1);
    return {m_anchor.spine, cl->pages[size_t(last)].end};
}

layout::Hit BookPage::hitAt(const QPointF& pos, int* pageOut) const
{
    const auto cl = current();
    if (pageOut) *pageOut = -1;
    if (!cl || cl->pages.empty()) return {};
    const int col = columnAt(pos);
    const int page = firstPage(*cl) + col;
    if (page >= int(cl->pages.size())) return {};
    if (pageOut) *pageOut = page;
    return layout::hitTest(*cl, page, pos - columnRect(col).topLeft());
}

void BookPage::applyParams()
{
    if (!m_app || width() < 50 || height() < 50) return;
    const Appearance a = m_app->appearance();
    const int columnsBefore = m_geo.columns;
    m_geo = geometryFor(size());
    if (m_geo.columns != columnsBefore) emit columnsChanged();
    m_turn->setDuration(m_app->pageTurnMs());
    m_turn->setEasingCurve(layout::easingForMotion(m_app->pageTurnMotion()));
    layout::LayoutParams p;
    p.columnWidth = m_geo.columnWidth;
    p.pageHeight = m_geo.pageHeight;
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
        // The chapter's last screen (two pages side by side: the pair that holds its last page).
        const int c = qMax(1, m_geo.columns), last = int(cl->pages.size()) - 1;
        m_anchor.offset = cl->pages[size_t(last - last % c)].start;
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
    if (!m_toImage.isNull()) {   // a jump during a turn: the turn ends at once
        m_turn->stop();
        m_fromImage = m_toImage = QImage();
        emit animatingChanged();
    }
    const int count = int(m_app->engine()->book()->spine.size());
    m_lastMoveByTurn = m_turning;
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
    const int next = firstPage(*cl) + m_geo.columns;
    if (next < int(cl->pages.size())) return turnTo({m_anchor.spine, cl->pages[size_t(next)].start}, 1);
    if (m_anchor.spine + 1 < int(m_app->engine()->book()->spine.size())) return turnTo({m_anchor.spine + 1, 0}, 1);
    return false;
}

bool BookPage::previousPage()
{
    const auto cl = current();
    if (!cl) return false;
    const int first = firstPage(*cl);
    if (first > 0) return turnTo({m_anchor.spine, cl->pages[size_t(qMax(0, first - m_geo.columns))].start}, -1);
    if (m_anchor.spine > 0) return turnTo({m_anchor.spine - 1, kEndOfChapter}, -1);
    return false;
}

bool BookPage::turnTo(const core::Position& p, int direction)
{
    // As the desktop: the screen before and after, the chosen animation between them. A chapter that isn't laid out
    // yet appears without it.
    const bool animate = m_app && m_app->pageTurnStyle() != QLatin1String("none") && !m_image.isNull();
    QImage from;
    if (animate) from = !m_toImage.isNull() ? m_toImage : m_image;   // a turn during a turn starts from its end
    const bool was = isAnimating();
    m_turn->stop();
    m_fromImage = m_toImage = QImage();
    m_turning = true;
    moveTo(p);
    m_turning = false;
    if (animate && current() && m_anchor.offset != kEndOfChapter) {
        m_fromImage = from;
        m_toImage = m_image;
        m_turnDir = direction;
        m_turn->start();
    }
    if (was != isAnimating()) emit animatingChanged();
    return true;
}

int BookPage::offsetAt(qreal x, qreal y, int* charOffset) const
{
    if (charOffset) *charOffset = -1;
    int page = -1;
    const layout::Hit h = hitAt(QPointF(x, y), &page);
    if (page < 0) return -1;
    if (charOffset) *charOffset = h.charOffset;
    return h.offset;
}

bool BookPage::tapAt(qreal x, qreal y)
{
    const auto cl = current();
    if (!m_app || !cl || cl->pages.empty()) return false;
    // A finger is less exact than a mouse: a short link mark (a footnote's "1") counts when the tap is near it.
    for (const QPointF d : {QPointF(0, 0), QPointF(-8, 0), QPointF(8, 0), QPointF(0, -8), QPointF(0, 8)}) {
        const layout::Hit h = hitAt(QPointF(x, y) + d);
        if (h.link) {
            const QString target = h.link->target;
            const bool external = h.link->external, noteRef = h.link->noteRef;
            m_app->followLink(target, external, noteRef);
            return true;
        }
        if (d.isNull() && h.image && !h.imagePath.isEmpty()) {
            m_app->showImage(h.imagePath);
            return true;
        }
    }
    return false;
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
    // A word inside a highlight: the whole highlight is taken (all its words), as on the desktop.
    if (m_app)
        for (const layout::Overlay& o : m_app->highlightsIn(m_anchor.spine))
            if (o.start <= s && o.end >= e) {
                s = o.start;
                e = o.end;
                break;
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
        const std::vector<std::pair<int, int>> wordMarks = m_app ? m_app->wordBookmarksIn(m_anchor.spine) : std::vector<std::pair<int, int>>();
        const int first = firstPage(*cl);
        for (int col = 0; col < m_geo.columns && first + col < int(cl->pages.size()); ++col) {
            const int page = first + col;
            const QPointF origin = columnRect(col).topLeft();
            layout::paintPage(p, *cl, page, origin, colors, overlays);
            // Word bookmarks: a small ribbon in the accent color over the word's first letter.
            p.setRenderHint(QPainter::Antialiasing);
            for (const auto& [ws, we] : wordMarks) {
                const std::vector<QRectF> rects = layout::rangeRects(*cl, page, ws, qMin(we, ws + 1));
                if (rects.empty()) continue;
                const QRectF r = rects.front().translated(origin);
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
            p.setRenderHint(QPainter::Antialiasing, false);
        }
    }
    m_image = img;
    update();
}

void BookPage::paint(QPainter* painter)
{
    if (m_image.isNull()) return;
    if (!m_toImage.isNull() && !m_fromImage.isNull()) {
        // A page turn: the shared styles (core/layout/PageTurn.h), the selection left out until it ends.
        const MobileApp::Colors c = m_app ? m_app->pageColors() : MobileApp::Colors{};
        painter->fillRect(QRectF(0, 0, width(), height()), c.bg);
        layout::paintPageTurn(*painter, m_app ? m_app->pageTurnStyle() : QStringLiteral("slide"), m_turn->currentValue().toDouble(), m_turnDir,
                              m_app && m_app->rightToLeft(), m_fromImage, m_toImage, width(), height(), c.bg, c.line, c.dark);
        return;
    }
    painter->drawImage(QRectF(0, 0, width(), height()), m_image);
    // The selection over the page: multiplied on a light page and screened on a dark one, so the text stays as
    // it is (as if it were under the words). It blinks gently: it's temporary, unlike a highlight.
    const auto cl = current();
    if (!cl || cl->pages.empty() || m_selSpine != m_anchor.spine || !hasSelection()) return;
    const MobileApp::Colors c = m_app ? m_app->pageColors() : MobileApp::Colors{};
    QColor sel = c.sel;
    sel.setAlphaF(float(qMin(1.0, sel.alphaF() * 1.6) * selectionStrength()));
    painter->setCompositionMode(c.dark ? QPainter::CompositionMode_Screen : QPainter::CompositionMode_Multiply);
    const int first = firstPage(*cl);
    for (int col = 0; col < m_geo.columns && first + col < int(cl->pages.size()); ++col)
        for (const QRectF& r : layout::rangeRects(*cl, first + col, m_selStart, m_selEnd))
            painter->fillRect(r.translated(columnRect(col).topLeft()), sel);
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

#include "PageView.h"

#include "Icons.h"

#include "core/layout/PageRenderer.h"
#include "core/services/BookEngine.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextBoundaryFinder>
#include <QWheelEvent>

#include <climits>

namespace {

constexpr int kEndOfChapter = INT_MAX;   // "the last screen of the chapter", resolved when its layout is ready

}  // namespace

PageView::PageView(core::BookEngine* engine, QWidget* parent) : QWidget(parent), m_engine(engine)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 300);
    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(120);
    connect(&m_resizeTimer, &QTimer::timeout, this, &PageView::applyParams);
    connect(m_engine, &core::BookEngine::layoutReady, this, &PageView::onLayoutReady);
    m_waiting.start();
}

void PageView::setTheme(const Theme& t)
{
    m_theme = t;
    update();
}

void PageView::setAppearance(const Appearance& a)
{
    m_look = a;
    m_engine->setBodyFont(a.font.isEmpty() ? QStringList() : QStringList{a.font});
    applyParams();
}

PageView::Geometry PageView::geometryFor(QSize size) const
{
    Geometry g;
    const float bodyPx = 20.f * float(m_look.textSizePct) / 100.f;
    const float minSide = float(m_look.marginPx), gap = qMax(48.f, 2.f * bodyPx + float(m_look.marginPx) / 2.f);
    const float availW = float(size.width()) - 2 * minSide;
    const float maxCol = qMax(200.f, float(m_look.pageWidthEm) * bodyPx);
    int cols = 1;
    if (m_look.columns == QLatin1String("two")) cols = 2;
    // Automatic: two columns only when each can be at least 90 % of the chosen page width.
    else if (m_look.columns == QLatin1String("auto") && availW >= 2 * qMin(maxCol, 30.f * bodyPx) * 0.9f + gap + 64.f) cols = 2;
    g.columns = cols;
    g.gap = gap;
    g.columnWidth = qMax(200.f, std::floor(qMin(maxCol, (availW - gap * float(cols - 1)) / float(cols))));
    const float used = g.columnWidth * float(cols) + gap * float(cols - 1);
    g.left = std::floor((float(size.width()) - used) / 2.f);
    g.top = 44;
    g.pageHeight = qMax(120.f, float(size.height()) - g.top - 48.f);   // the bottom margin also holds the Back chip
    return g;
}

int PageView::columnCount() const { return m_geo.columns; }

void PageView::applyParams()
{
    m_geo = geometryFor(size());
    layout::LayoutParams p;
    p.columnWidth = m_geo.columnWidth;
    p.pageHeight = std::floor(m_geo.pageHeight);
    p.pxPerCssPx = 1.25f * float(m_look.textSizePct) / 100.f;
    p.lineSpacing = float(m_look.lineSpacingPct) / 100.f;
    p.justify = m_look.justify;
    p.hyphenate = m_look.hyphenate;
    p.fontKey = m_look.font;
    p.devicePixelRatio = devicePixelRatioF();
    m_engine->setParams(p);
    if (m_engine->isOpen()) {
        m_engine->request(m_anchor.spine);
        m_waiting.restart();
    }
    update();
}

void PageView::resizeEvent(QResizeEvent*)
{
    if (m_geo.columnWidth <= 0 || !m_engine->isOpen()) {
        applyParams();
        return;
    }
    m_resizeTimer.start();
}

std::shared_ptr<const layout::ChapterLayout> PageView::current() const
{
    return const_cast<core::BookEngine*>(m_engine)->layout(m_anchor.spine);
}

bool PageView::isReady() const { return m_engine->isOpen() && current() != nullptr && !m_resizeTimer.isActive(); }

int PageView::firstPage(const layout::ChapterLayout& cl) const
{
    const int page = cl.pageForOffset(m_anchor.offset);
    const int c = qMax(1, m_geo.columns);
    return page - page % c;
}

core::Position PageView::screenEnd() const
{
    const auto cl = current();
    if (!cl || cl->pages.empty()) return m_anchor;
    const int first = firstPage(*cl);
    const int last = qMin(int(cl->pages.size()) - 1, first + m_geo.columns - 1);
    return {m_anchor.spine, cl->pages[size_t(last)].end};
}

void PageView::onLayoutReady(int spine)
{
    if (spine != m_anchor.spine) return;
    const auto cl = current();
    if (!cl) return;
    m_shown = cl;
    if (m_anchor.offset == kEndOfChapter && !cl->pages.empty()) {
        const int c = qMax(1, m_geo.columns);
        const int last = int(cl->pages.size()) - 1;
        m_anchor.offset = cl->pages[size_t(last - last % c)].start;
        emit positionChanged(m_anchor, true);
    }
    // Neighbours, so turning into them is instant.
    m_engine->request(spine + 1);
    m_engine->request(spine - 1);
    update();
    emit readyChanged();
}

void PageView::setPosition(const core::Position& p) { moveTo(p, false); }

void PageView::moveTo(const core::Position& p, bool byTurn)
{
    const int spineCount = m_engine->book() ? int(m_engine->book()->spine.size()) : 0;
    m_anchor = p;
    m_anchor.spine = qBound(0, m_anchor.spine, qMax(0, spineCount - 1));
    if (m_anchor.offset < 0) m_anchor.offset = 0;
    if (m_selSpine != m_anchor.spine) clearSelection();
    m_waiting.restart();
    if (const auto cl = current()) onLayoutReady(m_anchor.spine);
    update();
    if (m_anchor.offset != kEndOfChapter) emit positionChanged(m_anchor, byTurn);
}

bool PageView::canGoNext() const
{
    const auto cl = current();
    if (!m_engine->book()) return false;
    if (!cl) return true;
    return firstPage(*cl) + m_geo.columns < int(cl->pages.size()) || m_anchor.spine + 1 < int(m_engine->book()->spine.size());
}

bool PageView::canGoPrevious() const
{
    const auto cl = current();
    if (!m_engine->book()) return false;
    if (!cl) return m_anchor.spine > 0;
    return firstPage(*cl) > 0 || m_anchor.spine > 0;
}

bool PageView::nextScreen()
{
    const auto cl = current();
    if (!cl) return false;   // still laying out: ignore, the page appears in a moment
    const int next = firstPage(*cl) + m_geo.columns;
    if (next < int(cl->pages.size())) moveTo({m_anchor.spine, cl->pages[size_t(next)].start}, true);
    else if (m_anchor.spine + 1 < int(m_engine->book()->spine.size())) moveTo({m_anchor.spine + 1, 0}, true);
    else return false;
    emit pageTurnedByUser();
    return true;
}

bool PageView::previousScreen()
{
    const auto cl = current();
    if (!cl) return false;
    const int first = firstPage(*cl);
    if (first > 0) moveTo({m_anchor.spine, cl->pages[size_t(qMax(0, first - m_geo.columns))].start}, true);
    else if (m_anchor.spine > 0) moveTo({m_anchor.spine - 1, kEndOfChapter}, true);
    else return false;
    emit pageTurnedByUser();
    return true;
}

QRectF PageView::columnRect(int i) const
{
    return QRectF(m_geo.left + float(i) * (m_geo.columnWidth + m_geo.gap), m_geo.top, m_geo.columnWidth, m_geo.pageHeight);
}

int PageView::columnAt(const QPointF& pos) const
{
    for (int i = 0; i < m_geo.columns; ++i) {
        const QRectF r = columnRect(i).adjusted(-m_geo.gap / 2, -m_geo.top, m_geo.gap / 2, 32);
        if (r.contains(pos)) return i;
    }
    return -1;
}

layout::Hit PageView::hitAt(const QPointF& pos, int* column) const
{
    const auto cl = current();
    const int col = columnAt(pos);
    if (column) *column = col;
    if (!cl || col < 0) return {};
    const int page = firstPage(*cl) + col;
    if (page >= int(cl->pages.size())) return {};
    return layout::hitTest(*cl, page, pos - columnRect(col).topLeft());
}

int PageView::hoverSide(const QPointF& pos) const
{
    const float zone = qMax(64.f, m_geo.left - 8.f);
    if (pos.x() < zone && canGoPrevious()) return -1;
    if (pos.x() > width() - zone && canGoNext()) return 1;
    return 0;
}

void PageView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), m_theme.bg);
    auto cl = current();
    if (!cl) cl = (m_shown && m_shown->spine == m_anchor.spine) ? m_shown : nullptr;
    if (!cl) {
        if (m_engine->isOpen() && m_waiting.elapsed() > 250) {
            p.setPen(m_theme.muted);
            p.drawText(rect(), Qt::AlignCenter, tr("Laying out pages…"));
        }
        return;
    }
    const layout::RenderColors colors{m_theme.fg, m_theme.muted, m_theme.accent, m_theme.line, m_theme.surface};
    std::vector<layout::Overlay> overlays;
    if (m_selSpine == m_anchor.spine && hasSelection())
        overlays.push_back({qMin(m_selStart, m_selEnd), qMax(m_selStart, m_selEnd), m_theme.sel});
    const int first = firstPage(*cl);
    for (int i = 0; i < m_geo.columns; ++i) {
        const int page = first + i;
        if (page >= int(cl->pages.size())) break;
        layout::paintPage(p, *cl, page, columnRect(i).topLeft(), colors, overlays);
    }

    if (m_hover != 0) {
        const float zone = qMax(64.f, m_geo.left - 8.f);
        const QPointF c(m_hover < 0 ? zone / 2 : width() - zone / 2, height() / 2.0);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(m_theme.line, 1));
        p.setBrush(m_theme.surface);
        p.drawEllipse(c, 22, 22);
        const QIcon ic = icons::icon(m_hover < 0 ? "chevronLeft" : "chevronRight", m_theme.fg, 22);
        ic.paint(&p, QRect(int(c.x()) - 11, int(c.y()) - 11, 22, 22));
    }
}

void PageView::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    if (e->button() != Qt::LeftButton) return;
    m_pressed = true;
    m_dragging = false;
    m_pressPos = e->position();
    m_pressSide = hoverSide(e->position());
    m_pressHit = m_pressSide ? layout::Hit{} : hitAt(e->position());
    if (!m_pressSide && m_pressHit.offset >= 0 && !(e->modifiers() & Qt::ShiftModifier)) {
        m_selAnchor = m_pressHit.offset;
    } else if (!m_pressSide && m_pressHit.offset >= 0 && hasSelection()) {
        m_selEnd = m_pressHit.offset;   // Shift+click extends
        m_dragging = true;
        update();
    }
}

void PageView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_pressed && !m_pressSide) {
        if (!m_dragging && (e->position() - m_pressPos).manhattanLength() > 4) {
            m_dragging = true;
            m_selSpine = m_anchor.spine;
            m_selStart = m_selEnd = m_selAnchor;
            emit selectionCleared();
        }
        if (m_dragging) {
            const layout::Hit h = hitAt(e->position());
            if (h.offset >= 0) {
                m_selEnd = h.offset;
                update();
            }
        }
        return;
    }
    const int side = hoverSide(e->position());
    if (side != m_hover) {
        m_hover = side;
        update();
    }
    const layout::Hit h = side ? layout::Hit{} : hitAt(e->position());
    setCursor(side || h.link || h.image ? Qt::PointingHandCursor : h.charOffset >= 0 ? Qt::IBeamCursor : Qt::ArrowCursor);
}

void PageView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_pressed) return;
    m_pressed = false;
    if (m_dragging) {
        m_dragging = false;
        if (m_selStart > m_selEnd) std::swap(m_selStart, m_selEnd);
        update();
        if (hasSelection() && !selectedText().trimmed().isEmpty()) emit selectionMade();
        return;
    }
    if (m_pressSide) {
        if (hoverSide(e->position()) == m_pressSide) m_pressSide < 0 ? previousScreen() : nextScreen();
        return;
    }
    clearSelection();
    if (m_pressHit.link) {
        LinkClick l{m_pressHit.link->target, m_pressHit.link->external, m_pressHit.link->noteRef};
        emit linkClicked(l, e->globalPosition().toPoint());
    } else if (m_pressHit.image) {
        emit imageClicked(m_pressHit.imagePath);
    }
}

void PageView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    m_pressed = false;
    if (selectWordAt(e->position()) && !selectedText().trimmed().isEmpty()) emit selectionMade();
}

bool PageView::selectWordAt(const QPointF& pos)
{
    const auto cl = current();
    const layout::Hit h = hitAt(pos);
    if (!cl || h.charOffset < 0) return false;
    for (const layout::Block& b : cl->flow->blocks) {
        if (b.kind != layout::Block::Text || h.charOffset < b.start || h.charOffset >= b.start + b.length) continue;
        QTextBoundaryFinder f(QTextBoundaryFinder::Word, b.text);
        const int at = h.charOffset - b.start;
        f.setPosition(at);
        int s = f.isAtBoundary() && (f.boundaryReasons() & QTextBoundaryFinder::StartOfItem) ? at : int(f.toPreviousBoundary());
        f.setPosition(at);
        int en = int(f.toNextBoundary());
        if (s < 0) s = 0;
        if (en < 0) en = int(b.text.size());
        // A click on the space or punctuation after a word selects that word, without the space.
        auto isWordChar = [&](int i) { return i >= 0 && i < int(b.text.size()) && (b.text[i].isLetterOrNumber() || b.text[i] == QChar(0x00AD)); };
        if (!isWordChar(at) && isWordChar(at - 1)) {
            en = at;
            f.setPosition(at - 1);
            s = f.isAtBoundary() ? at - 1 : qMax(0, int(f.toPreviousBoundary()));
        }
        while (en > s && !isWordChar(en - 1)) --en;
        while (s < en && !isWordChar(s)) ++s;
        if (s >= en) return false;
        m_selSpine = m_anchor.spine;
        m_selStart = b.start + s;
        m_selEnd = b.start + en;
        update();
        return true;
    }
    return false;
}

void PageView::contextMenuEvent(QContextMenuEvent* e)
{
    // Inside the selection: the menu is for it. Elsewhere on text: the word there is selected first, as in Kindle.
    const layout::Hit h = hitAt(e->pos());
    const bool inSelection = hasSelection() && m_selSpine == m_anchor.spine && h.charOffset >= selectionStart() &&
                             h.charOffset < selectionEnd();
    if (!inSelection) {
        if (!selectWordAt(e->pos())) {
            clearSelection();
            return;
        }
    }
    if (selectedText().trimmed().isEmpty()) return;
    m_pressed = false;
    emit selectionMenuRequested(e->globalPos());
}

QString PageView::selectionSentence() const
{
    const auto cl = current();
    if (!cl || m_selSpine != m_anchor.spine || !hasSelection()) return {};
    const int from = selectionStart(), to = selectionEnd();
    for (const layout::Block& b : cl->flow->blocks) {
        if (b.kind != layout::Block::Text || from < b.start || from >= b.start + b.length) continue;
        QTextBoundaryFinder f(QTextBoundaryFinder::Sentence, b.text);
        f.setPosition(from - b.start);
        int s = f.isAtBoundary() ? from - b.start : int(f.toPreviousBoundary());
        if (s < 0) s = 0;
        f.setPosition(qMin(int(b.text.size()), qMax(to - b.start, s + 1)));
        int en = f.isAtBoundary() ? f.position() : int(f.toNextBoundary());
        if (en < 0) en = int(b.text.size());
        return QString(b.text.mid(s, en - s)).simplified();
    }
    return {};
}

void PageView::wheelEvent(QWheelEvent* e)
{
    int d = e->angleDelta().y();
    if (d == 0) d = -e->angleDelta().x();
    m_wheel += d;
    while (m_wheel <= -120) {
        m_wheel += 120;
        nextScreen();
    }
    while (m_wheel >= 120) {
        m_wheel -= 120;
        previousScreen();
    }
    e->accept();
}

void PageView::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_PageDown:
        nextScreen();
        return;
    case Qt::Key_Space:
        (e->modifiers() & Qt::ShiftModifier) ? previousScreen() : nextScreen();
        return;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp:
        previousScreen();
        return;
    case Qt::Key_Home:
        moveTo({m_anchor.spine, 0}, false);
        return;
    case Qt::Key_End:
        moveTo({m_anchor.spine, kEndOfChapter}, false);
        return;
    case Qt::Key_Escape:
        if (hasSelection()) {
            clearSelection();
            return;
        }
        break;
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}

void PageView::leaveEvent(QEvent*)
{
    if (m_hover) {
        m_hover = 0;
        update();
    }
}

QString PageView::selectedText() const
{
    const auto cl = current();
    if (!cl || m_selSpine != m_anchor.spine || !hasSelection()) return {};
    return cl->flow->textRange(qMin(m_selStart, m_selEnd), qMax(m_selStart, m_selEnd));
}

void PageView::clearSelection()
{
    if (m_selStart == m_selEnd && m_selSpine < 0) return;
    m_selStart = m_selEnd = 0;
    m_selSpine = -1;
    update();
    emit selectionCleared();
}

void PageView::selectRange(const core::Position& from, const core::Position& to)
{
    m_selSpine = from.spine;
    m_selStart = from.offset;
    m_selEnd = to.offset;
    update();
}

QRect PageView::rangeRect(int spine, int start, int end) const
{
    const auto cl = current();
    if (!cl || spine != m_anchor.spine) return {};
    QRectF out;
    const int first = firstPage(*cl);
    for (int i = 0; i < m_geo.columns && first + i < int(cl->pages.size()); ++i)
        for (const QRectF& r : layout::rangeRects(*cl, first + i, start, end)) out |= r.translated(columnRect(i).topLeft());
    return out.toAlignedRect();
}

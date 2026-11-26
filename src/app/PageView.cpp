#include "PageView.h"

#include <cmath>
#include <numbers>

#include "Icons.h"

#include "core/layout/PageRenderer.h"
#include "core/services/BookEngine.h"

#include <QContextMenuEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextBoundaryFinder>
#include <QVariantAnimation>
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
    m_blinkTimer.setInterval(60);
    connect(&m_blinkTimer, &QTimer::timeout, this, &PageView::blinkStep);
    m_turn = new QVariantAnimation(this);
    m_turn->setStartValue(0.0);
    m_turn->setEndValue(1.0);
    connect(m_turn, &QVariantAnimation::valueChanged, this, [this] { update(); });
    connect(m_turn, &QVariantAnimation::finished, this, [this] {
        m_fromPix = m_toPix = QPixmap();
        update();
    });
    setPageTurn(QStringLiteral("none"), 300, QStringLiteral("smooth"));
    m_noticeTimer.setSingleShot(true);
    m_noticeTimer.setInterval(1400);
    connect(&m_noticeTimer, &QTimer::timeout, this, [this] {
        m_notice.clear();
        update();
    });
}

void PageView::setPageTurn(const QString& style, int ms, const QString& motion)
{
    m_turnStyle = style;
    m_turn->setDuration(qBound(100, ms, 1000));
    m_turn->setEasingCurve(easingFor(motion));
}

QEasingCurve::Type PageView::easingFor(const QString& motion)
{
    // How a turn speeds up and slows down (Text and Layout > Motion; many more since the owner's request of
    // 2026-11-26). Unknown keys: Smooth.
    static const QHash<QString, QEasingCurve::Type> curves{
        {QStringLiteral("smooth"), QEasingCurve::OutCubic},     {QStringLiteral("even"), QEasingCurve::Linear},
        {QStringLiteral("gentle"), QEasingCurve::InOutSine},    {QStringLiteral("soft"), QEasingCurve::OutSine},
        {QStringLiteral("quick"), QEasingCurve::OutQuint},      {QStringLiteral("snappy"), QEasingCurve::OutExpo},
        {QStringLiteral("rounded"), QEasingCurve::OutCirc},     {QStringLiteral("speeding"), QEasingCurve::InCubic},
        {QStringLiteral("ease"), QEasingCurve::InOutCubic},     {QStringLiteral("strong"), QEasingCurve::InOutQuint},
        {QStringLiteral("overshoot"), QEasingCurve::OutBack},   {QStringLiteral("windup"), QEasingCurve::InOutBack},
        {QStringLiteral("springy"), QEasingCurve::OutElastic},  {QStringLiteral("bounce"), QEasingCurve::OutBounce}};
    return curves.value(motion, QEasingCurve::OutCubic);
}

QStringList PageView::motions()
{
    return {"smooth", "even", "gentle", "soft", "quick", "snappy", "rounded", "speeding", "ease", "strong", "overshoot", "windup", "springy", "bounce"};
}

bool PageView::isAnimating() const { return m_turn->state() == QAbstractAnimation::Running || !m_toPix.isNull(); }

void PageView::setTurnProgressForTests(double t)
{
    if (m_toPix.isNull()) return;
    m_turn->pause();
    m_turn->setCurrentTime(int(t * m_turn->duration()));
    update();
}

void PageView::showNotice(const QString& text)
{
    m_notice = text;
    m_noticeTimer.start();
    update();
}

QPixmap PageView::renderScreen()
{
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(size() * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(m_theme.bg);
    QPainter p(&pm);
    paintScreen(p);
    return pm;
}

bool PageView::turnTo(const core::Position& p, int direction)
{
    const bool animate = m_turnStyle != QLatin1String("none") && width() > 0 && height() > 0;
    QPixmap from;
    if (animate) from = !m_toPix.isNull() ? m_toPix : renderScreen();   // a turn during a turn starts from its end
    m_turn->stop();
    m_fromPix = m_toPix = QPixmap();
    moveTo(p, true);
    // A chapter that isn't laid out yet appears without the animation.
    if (animate && current()) {
        m_fromPix = from;
        m_toPix = renderScreen();
        m_turnDir = direction;
        m_turn->start();
    }
    emit pageTurnedByUser();
    return true;
}

void PageView::paintTurn(QPainter& p)
{
    const double t = m_turn->currentValue().toDouble();
    // Turning back is turning forward played backwards: the new screen is the one being turned away.
    const QPixmap& a = m_turnDir > 0 ? m_fromPix : m_toPix;   // the page that leaves (forward)
    const QPixmap& b = m_turnDir > 0 ? m_toPix : m_fromPix;   // the page that arrives (forward)
    double s = m_turnDir > 0 ? t : 1.0 - t;
    // Overshoot, Wind-Up and Springy go past the ends (s below 0 or above 1): the sliding styles show that, the
    // others stay within the turn.
    static const QStringList pastTheEnds{QStringLiteral("slide"), QStringLiteral("cover"), QStringLiteral("uncover"), QStringLiteral("card")};
    if (!pastTheEnds.contains(m_turnStyle)) s = qBound(0.0, s, 1.0);
    const qreal w = width(), h = height();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    auto shadow = [&](qreal x, qreal width, bool toRight, qreal strength) {
        QLinearGradient g(x, 0, toRight ? x + width : x - width, 0);
        g.setColorAt(0, QColor(0, 0, 0, int(110 * strength)));
        g.setColorAt(1, QColor(0, 0, 0, 0));
        p.fillRect(QRectF(toRight ? x : x - width, 0, width, h), g);
    };
    // Right to left, the next page comes in from the left.
    const qreal dir = m_rtl ? -1.0 : 1.0;
    if (m_turnStyle == QLatin1String("slide")) {
        p.drawPixmap(QPointF(-s * w * dir, 0), a);
        p.drawPixmap(QPointF((1 - s) * w * dir, 0), b);
    } else if (m_turnStyle == QLatin1String("cover")) {
        p.drawPixmap(QPointF(0, 0), a);
        const qreal x = (1 - s) * w * dir;
        if (m_rtl) shadow(x + w, 24, true, 1.0 - s * 0.5);
        else shadow(x, 24, false, 1.0 - s * 0.5);
        p.drawPixmap(QPointF(x, 0), b);
    } else if (m_turnStyle == QLatin1String("fade")) {
        p.drawPixmap(QPointF(0, 0), a);
        p.setOpacity(s);
        p.drawPixmap(QPointF(0, 0), b);
        p.setOpacity(1);
    } else if (m_turnStyle == QLatin1String("uncover")) {
        // Cover the other way: the old page slides away, the new one lies under it.
        p.drawPixmap(QPointF(0, 0), b);
        const qreal x = -s * w * dir;
        if (m_rtl) shadow(x, 24, false, 1.0 - s * 0.5);
        else shadow(x + w, 24, true, 1.0 - s * 0.5);
        p.drawPixmap(QPointF(x, 0), a);
    } else if (m_turnStyle == QLatin1String("flip") || m_turnStyle == QLatin1String("card")) {
        // In perspective, about an upright axis: a hardcover page swings over on its spine (the left edge; the
        // right one right to left) and the new page lies under it; a card turns over about its middle, the new
        // page on its back.
        const bool card = m_turnStyle == QLatin1String("card");
        auto turned = [&](const QPixmap& pm, qreal axisX, qreal degrees, qreal darken) {
            QTransform tr;
            tr.translate(axisX, h / 2);
            tr.rotate(degrees, Qt::YAxis, qMax<qreal>(1024, w * 1.6));
            tr.translate(-axisX, -h / 2);
            p.save();
            p.setTransform(tr, true);
            p.drawPixmap(QPointF(0, 0), pm);
            if (darken > 0) p.fillRect(QRectF(0, 0, w, h), QColor(0, 0, 0, int(140 * darken)));
            p.restore();
        };
        if (card) {
            // The first half the old page turns edge-on, the second the new one turns to face the reader.
            const qreal deg = s < 0.5 ? s * 180 : s * 180 - 180;
            const qreal darken = s < 0.5 ? s * 2 * 0.5 : (1 - s) * 2 * 0.5;
            turned(s < 0.5 ? a : b, w / 2, deg * dir, darken);
        } else {
            p.drawPixmap(QPointF(0, 0), b);
            shadow(m_rtl ? w * s : w * (1 - s), qMin<qreal>(64, w * 0.2), m_rtl, 1.0 - s);
            turned(a, m_rtl ? w : 0, -s * 90 * dir, s * 0.6);
        }
    } else {   // curl: the page folds back from the right edge, its back lying over the page that is left
        const qreal x = w * (1 - s);              // the fold
        const qreal flap = qMin(w - x, x);        // the folded part, lying left of the fold
        p.drawPixmap(QPointF(0, 0), b);
        shadow(x, qMin<qreal>(48, w - x), true, 1.0 - s);
        p.save();
        p.setClipRect(QRectF(0, 0, x, h));
        p.drawPixmap(QPointF(0, 0), a);
        p.restore();
        if (flap > 1) {
            const QRectF back(x - flap, 0, flap, h);
            shadow(x - flap, qMin<qreal>(24, x - flap), false, 0.6);
            p.fillRect(back, m_theme.bg);
            // The page's own print showing through its back, mirrored.
            p.save();
            p.setClipRect(back);
            p.setOpacity(0.06);
            p.translate(2 * x, 0);
            p.scale(-1, 1);
            p.drawPixmap(QPointF(0, 0), a);
            p.restore();
            QLinearGradient g(x - flap, 0, x, 0);
            g.setColorAt(0, QColor(255, 255, 255, m_theme.dark ? 18 : 60));
            g.setColorAt(0.7, QColor(0, 0, 0, 0));
            g.setColorAt(1, QColor(0, 0, 0, 70));
            p.fillRect(back, g);
            p.setPen(QPen(m_theme.line, 1));
            p.drawLine(QPointF(x - flap, 0), QPointF(x - flap, h));
        }
    }
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
    g.width = size.width();
    return g;
}

int PageView::columnCount() const { return m_geo.columns; }

void PageView::applyParams()
{
    const int columnsBefore = m_geo.columns;
    m_geo = geometryFor(size());
    if (m_geo.columns != columnsBefore) emit columnsChanged();   // one page or two, with the same column width
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
    emit resized();   // pop-ups placed next to text would point at the wrong place
    if (m_hold && m_engine->isOpen()) {
        // Laid out for this width now, or when the layout being made is ready (then for the width at that time).
        if (!m_liveBusy) startLiveLayout();
        update();   // meanwhile the last layout, in its own columns, centered (paintScreen)
        return;
    }
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

core::Position PageView::screenStart() const
{
    const auto cl = current();
    if (!cl || cl->pages.empty()) return m_anchor;
    return {m_anchor.spine, cl->pages[size_t(firstPage(*cl))].start};
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
    m_shownGeo = m_geo;
    if (m_anchor.offset == kEndOfChapter && !cl->pages.empty()) {
        const int c = qMax(1, m_geo.columns);
        const int last = int(cl->pages.size()) - 1;
        m_anchor.offset = cl->pages[size_t(last - last % c)].start;
        emit positionChanged(m_anchor, true);
    }
    update();
    if (m_hold) {
        // Dragging a panel's edge: the next layout, for the width now (no neighbours: they'd be out of date at
        // once).
        m_liveBusy = false;
        if (size().width() != m_geo.width) startLiveLayout();
        emit readyChanged();
        return;
    }
    // Neighbours, so turning into them is instant.
    m_engine->request(spine + 1);
    m_engine->request(spine - 1);
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

void PageView::flushResize()
{
    // A turn right after a resize must count pages with the new size: with the old one (two pages
    // shown, one now) it would skip a page.
    if (!m_resizeTimer.isActive()) return;
    m_resizeTimer.stop();
    applyParams();
}

void PageView::setHoldLayout(bool on)
{
    // Dragging the Practice panel's edge: the panel follows the mouse, and so does the book's text, one layout at a
    // time. Laid out at every pause of the drag (after the resize's pause), the old pages were drawn at the new
    // positions meanwhile and the text jumped about; kept as it was until release, the text stopped following once
    // the page was narrower than its column (the owner's reports, 2026-11-25 and 2026-11-26).
    if (m_hold == on) return;
    m_hold = on;
    m_resizeTimer.stop();
    if (on) return;
    m_liveBusy = false;
    applyParams();   // the final width (nothing is laid out again when it is the one laid out last)
}

void PageView::startLiveLayout()
{
    applyParams();
    m_liveBusy = current() == nullptr;   // the same parameters as before: nothing to wait for
}

bool PageView::nextScreen()
{
    flushResize();
    const auto cl = current();
    if (!cl) return false;   // still laying out: ignore, the page appears in a moment
    const int next = firstPage(*cl) + m_geo.columns;
    if (next < int(cl->pages.size())) return turnTo({m_anchor.spine, cl->pages[size_t(next)].start}, 1);
    if (m_anchor.spine + 1 < int(m_engine->book()->spine.size())) return turnTo({m_anchor.spine + 1, 0}, 1);
    return false;
}

bool PageView::previousScreen()
{
    flushResize();
    const auto cl = current();
    if (!cl) return false;
    const int first = firstPage(*cl);
    if (first > 0) return turnTo({m_anchor.spine, cl->pages[size_t(qMax(0, first - m_geo.columns))].start}, -1);
    if (m_anchor.spine > 0) return turnTo({m_anchor.spine - 1, kEndOfChapter}, -1);
    return false;
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

float PageView::turnZone() const
{
    // Only a strip at the window's edge turns pages (the owner's request, 2026-11-23: clicks in the white margin
    // beside the text turned pages by mistake): 80 px, less when the margin is narrower (it stops 8 px short of the
    // text), at least 40. It was 56 px at most until 2026-11-26: a little too thin (the owner's report).
    return qBound(40.f, m_geo.left - 8.f, 80.f);
}

bool PageView::inTurnBand(qreal y) const
{
    // Only the middle of the page's height turns pages (the owner's report, 2026-11-26: a click a little below
    // the Practice button, at the top right, turned the page): the middle 75 % (60 % at first was a little short,
    // the owner's report of the same day), at least 60 px from the top and the bottom.
    const qreal h = height();
    return y >= qMax<qreal>(60, h * 0.125) && y <= qMin<qreal>(h - 60, h * 0.875);
}

int PageView::hoverSide(const QPointF& pos) const
{
    if (!inTurnBand(pos.y())) return 0;
    const float zone = turnZone();
    if (pos.x() < zone && (m_rtl ? canGoNext() : canGoPrevious())) return -1;
    if (pos.x() > width() - zone && (m_rtl ? canGoPrevious() : canGoNext())) return 1;
    return 0;
}

void PageView::turnFromSide(int side)
{
    if ((m_rtl ? -side : side) < 0) previousScreen();
    else nextScreen();
}

void PageView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), m_theme.bg);
    if (!m_toPix.isNull()) paintTurn(p);
    else paintScreen(p);

    if (m_toPix.isNull() && m_engine->isOpen()) {
        const float zone = turnZone();
        const float cx = qMax(zone / 2, 24.f);   // the whole arrow shows, even beside a narrow strip
        p.setRenderHint(QPainter::Antialiasing);
        for (int side : {-1, 1}) {
            const QPointF c(side < 0 ? cx : width() - cx, height() / 2.0);
            if (m_hover == side) {
                // Pointed at: the arrow in a circle.
                p.setPen(QPen(m_theme.line, 1));
                p.setBrush(m_theme.surface);
                p.drawEllipse(c, 22, 22);
                const QIcon ic = icons::icon(side < 0 ? "chevronLeft" : "chevronRight", m_theme.fg, 22);
                ic.paint(&p, QRect(int(c.x()) - 11, int(c.y()) - 11, 22, 22));
            } else if (m_turnArrows && (side < 0 ? (m_rtl ? canGoNext() : canGoPrevious()) : (m_rtl ? canGoPrevious() : canGoNext()))) {
                // Always there, faintly: a click at the side turns the page (the owner's request, 2026-11-26).
                const QIcon ic = icons::icon(side < 0 ? "chevronLeft" : "chevronRight", m_theme.muted, 22);
                p.setOpacity(0.55);
                ic.paint(&p, QRect(int(c.x()) - 11, int(c.y()) - 11, 22, 22));
                p.setOpacity(1);
            }
        }
    }
    if (!m_notice.isEmpty()) {
        p.setRenderHint(QPainter::Antialiasing);
        QFont f = font();
        f.setPixelSize(13);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        const QFontMetrics fm(f);
        const QRectF pill(width() / 2.0 - (fm.horizontalAdvance(m_notice) + 32) / 2.0, 10, fm.horizontalAdvance(m_notice) + 32, 30);
        p.setPen(QPen(m_theme.line, 1));
        p.setBrush(m_theme.pop);
        p.drawRoundedRect(pill, 15, 15);
        p.setPen(m_theme.fg);
        p.drawText(pill, Qt::AlignCenter, m_notice);
    }
}

void PageView::paintScreen(QPainter& p)
{
    auto cl = current();
    const bool fresh = cl != nullptr;
    if (!cl) cl = (m_shown && m_shown->spine == m_anchor.spine) ? m_shown : nullptr;
    if (!cl) {
        if (m_engine->isOpen() && m_waiting.elapsed() > 250) {
            p.setPen(m_theme.muted);
            p.drawText(rect(), Qt::AlignCenter, tr("Laying out pages…"));
        }
        return;
    }
    // The pages are drawn with the geometry they were laid out for: the last layout, while the new one is being
    // made, keeps its own columns (drawn with the new ones, the text jumped about). In a width other than the one
    // they were made for (a panel's edge being dragged, or the new layout not ready yet), they are centered.
    struct Restore {
        Geometry& target;
        Geometry saved;
        ~Restore() { target = saved; }
    } restore{m_geo, m_geo};
    if (!fresh) m_geo = m_shownGeo;
    if (m_geo.width != width()) {
        const float used = m_geo.columnWidth * float(m_geo.columns) + m_geo.gap * float(m_geo.columns - 1);
        m_geo.left = qMax(qMin(float(m_look.marginPx), m_geo.left), std::floor((float(width()) - used) / 2.f));
    }
    const layout::RenderColors colors{m_theme.fg, m_theme.muted, m_theme.accent, m_theme.line, m_theme.surface};
    std::vector<layout::Overlay> overlays;
    if (m_highlightSpine == m_anchor.spine) overlays = m_highlights;
    if (m_selSpine == m_anchor.spine && hasSelection()) {
        QColor sel = m_theme.sel;
        sel.setAlphaF(float(sel.alphaF() * selectionStrength()));
        overlays.push_back({qMin(m_selStart, m_selEnd), qMax(m_selStart, m_selEnd), sel});
        if (selectionBlinks() && !m_blinkTimer.isActive()) {
            m_blinkClock.start();
            m_blinkTimer.start();
        }
    }
    const int first = firstPage(*cl);
    for (int i = 0; i < m_geo.columns; ++i) {
        const int page = first + i;
        if (page >= int(cl->pages.size())) break;
        layout::paintPage(p, *cl, page, columnRect(i).topLeft(), colors, overlays);
        // Word bookmarks: a small ribbon in the accent color over the word's first letter.
        if (m_wordMarksSpine != m_anchor.spine) continue;
        for (const auto& [ws, we] : m_wordMarks) {
            const std::vector<QRectF> rects = layout::rangeRects(*cl, page, ws, qMin(we, ws + 1));
            if (rects.empty()) continue;
            const QRectF r = rects.front().translated(columnRect(i).topLeft());
            const qreal w = qBound(6.0, r.height() * 0.32, 10.0), h = w * 1.5, x = r.left(), y = r.top() - h * 0.45;
            QPainterPath ribbon;
            ribbon.moveTo(x, y);
            ribbon.lineTo(x + w, y);
            ribbon.lineTo(x + w, y + h);
            ribbon.lineTo(x + w / 2, y + h - w * 0.45);
            ribbon.lineTo(x, y + h);
            ribbon.closeSubpath();
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            p.fillPath(ribbon, m_theme.accent);
            p.restore();
        }
    }
}

void PageView::setWordBookmarks(int spine, const std::vector<std::pair<int, int>>& words)
{
    m_wordMarksSpine = spine;
    m_wordMarks = words;
    update();
}

namespace {
int g_blink = 1;   // 0 never (tests), 1 when the window is active, 2 always (tests: off-screen windows aren't active)
}

void PageView::setSelectionBlinkForTests(bool on) { g_blink = on ? 2 : 0; }

bool PageView::selectionBlinks() const
{
    // Not while it is being dragged out, and only where someone can see it.
    if (!g_blink || !hasSelection() || m_selSpine != m_anchor.spine || m_dragging || m_pressed || !m_toPix.isNull()) return false;
    return g_blink == 2 || (isVisible() && window() && window()->isActiveWindow());
}

double PageView::selectionStrength() const
{
    if (!m_blinkTimer.isActive() || !selectionBlinks()) return 1.0;
    // A soft blink, once a second: full, fading to a fifth, and back.
    const double phase = 0.5 + 0.5 * std::cos(2.0 * std::numbers::pi * double(m_blinkClock.elapsed() % 1000) / 1000.0);
    return 0.2 + 0.8 * phase;
}

void PageView::blinkStep()
{
    if (!selectionBlinks()) {
        m_blinkTimer.stop();
        update();   // painted steady again
        return;
    }
    // Only the selection's area is painted again.
    const QRect r = rangeRect(m_selSpine, selectionStart(), selectionEnd()).adjusted(-6, -6, 6, 6);
    update(r.united(m_blinkRect));
    m_blinkRect = r;
}

void PageView::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    if (e->button() != Qt::LeftButton) return;
    m_pressed = true;
    m_dragging = false;
    m_wordDrag = false;
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
    if (m_pressed && m_wordDrag) {
        if (!m_dragging && (e->position() - m_pressPos).manhattanLength() > 4) {
            m_dragging = true;
            emit selectionCleared();   // the pop-up for the first word closes; it opens again on release
        }
        if (m_dragging) extendWordSelection(e->position());
        return;
    }
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
    if (m_wordDrag) {
        // The release of a double-click: the word stays selected; after a drag, the words selected.
        m_wordDrag = false;
        if (!m_dragging) return;
        m_dragging = false;
        update();
        if (hasSelection() && !selectedText().trimmed().isEmpty()) emit selectionMade();
        return;
    }
    if (m_dragging) {
        m_dragging = false;
        if (m_selStart > m_selEnd) std::swap(m_selStart, m_selEnd);
        update();
        if (hasSelection() && !selectedText().trimmed().isEmpty()) emit selectionMade();
        return;
    }
    emit pageClicked();
    if (m_pressSide) {
        if (hoverSide(e->position()) == m_pressSide) turnFromSide(m_pressSide);
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
    // Clicking an arrow quickly twice turns two pages; it never selects a word under the arrow.
    if (const int side = hoverSide(e->position())) {
        turnFromSide(side);
        return;
    }
    if (selectWordAt(e->position()) && !selectedText().trimmed().isEmpty()) {
        // Holding the button after a double-click and dragging selects whole words up to the pointer.
        m_pressed = true;
        m_wordDrag = true;
        m_dragging = false;
        m_pressPos = e->position();
        m_wordStart = m_selStart;
        m_wordEnd = m_selEnd;
        emit selectionMade();
    }
}

void PageView::extendWordSelection(const QPointF& pos)
{
    const layout::Hit h = hitAt(pos);
    if (h.offset < 0) return;
    int s = h.offset, en = h.offset;
    wordRangeAt(pos, &s, &en);   // between words: the pointer's own place
    if (s >= m_wordStart) {
        m_selStart = m_wordStart;
        m_selEnd = qMax(m_wordEnd, en);
    } else {
        m_selStart = s;
        m_selEnd = m_wordEnd;
    }
    update();
}

bool PageView::selectWordAt(const QPointF& pos)
{
    int s = 0, en = 0;
    if (!wordRangeAt(pos, &s, &en)) return false;
    m_selSpine = m_anchor.spine;
    m_selStart = s;
    m_selEnd = en;
    update();
    return true;
}

bool PageView::wordRangeAt(const QPointF& pos, int* start, int* end)
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
        *start = b.start + s;
        *end = b.start + en;
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
        // A word inside a highlight: the menu is for the whole highlight (all its words, the owner's report,
        // 2026-11-23), not the one word clicked.
        if (m_highlightSpine == m_anchor.spine)
            for (const layout::Overlay& o : m_highlights)
                if (o.start <= m_selStart && o.end >= m_selEnd && o.end - o.start > m_selEnd - m_selStart) {
                    m_selStart = o.start;
                    m_selEnd = o.end;
                    update();
                    break;
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
    // Ctrl+wheel: text size; Ctrl+Shift+wheel: margins (wheel up: larger, wider).
    if (e->modifiers() & Qt::ControlModifier) {
        int d = e->angleDelta().y();
        if (d == 0) d = e->angleDelta().x();   // some systems turn Shift+wheel into a sideways scroll
        m_zoomWheel += d;
        const bool margins = e->modifiers() & Qt::ShiftModifier;
        while (qAbs(m_zoomWheel) >= 120) {
            const int step = m_zoomWheel > 0 ? 1 : -1;
            m_zoomWheel -= step * 120;
            margins ? emit marginsRequested(step) : emit zoomRequested(step);
        }
        e->accept();
        return;
    }
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
        m_rtl ? previousScreen() : nextScreen();
        return;
    case Qt::Key_Left:
        m_rtl ? nextScreen() : previousScreen();
        return;
    case Qt::Key_Down:
    case Qt::Key_PageDown:
        nextScreen();
        return;
    case Qt::Key_Space:
        (e->modifiers() & Qt::ShiftModifier) ? previousScreen() : nextScreen();
        return;
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

void PageView::setHighlights(int spine, const std::vector<layout::Overlay>& overlays)
{
    m_highlightSpine = spine;
    m_highlights = overlays;
    update();
}

int PageView::offsetAt(const QPointF& pos) const { return hitAt(pos).charOffset; }

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

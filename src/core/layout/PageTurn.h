// Page-turn animations, shared by the desktop's page and the phone's: a picture of the screen before the turn and
// one after it, drawn at a point `t` (0..1, after the Motion's easing) of the turn, in one of the styles of Text and
// Layout > Animation: slide, cover, fade, uncover, flip (hardcover), card, curl.
#pragma once

#include <QColor>
#include <QEasingCurve>
#include <QHash>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QStringList>
#include <QTransform>

namespace layout {

// How a turn speeds up and slows down (Text and Layout > Motion). Unknown keys: Smooth.
inline QEasingCurve::Type easingForMotion(const QString& motion)
{
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

inline QStringList turnMotions()
{
    return {"smooth", "even", "gentle", "soft", "quick", "snappy", "rounded", "speeding", "ease", "strong", "overshoot", "windup", "springy", "bounce"};
}

inline QStringList turnStyles() { return {"none", "slide", "cover", "fade", "curl", "uncover", "flip", "card"}; }

namespace detail {
inline void draw(QPainter& p, const QPointF& at, const QPixmap& pm) { p.drawPixmap(at, pm); }
inline void draw(QPainter& p, const QPointF& at, const QImage& img) { p.drawImage(at, img); }
}  // namespace detail

// `t`: the animation's value (it may go a little past 0..1 with Overshoot, Wind-Up and Springy); `direction` 1
// forward, -1 back; `rtl`: pages turn right to left. The pictures are the whole w × h screen.
template <class Picture>
void paintPageTurn(QPainter& p, const QString& style, double t, int direction, bool rtl, const Picture& from, const Picture& to,
                   qreal w, qreal h, const QColor& bg, const QColor& line, bool dark)
{
    using detail::draw;
    // Turning back is turning forward played backwards: the new screen is the one being turned away.
    const Picture& a = direction > 0 ? from : to;   // the page that leaves (forward)
    const Picture& b = direction > 0 ? to : from;   // the page that arrives (forward)
    double s = direction > 0 ? t : 1.0 - t;
    // Overshoot, Wind-Up and Springy go past the ends (s below 0 or above 1): the sliding styles show that, the
    // others stay within the turn.
    static const QStringList pastTheEnds{QStringLiteral("slide"), QStringLiteral("cover"), QStringLiteral("uncover"), QStringLiteral("card")};
    if (!pastTheEnds.contains(style)) s = qBound(0.0, s, 1.0);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    auto shadow = [&](qreal x, qreal width, bool toRight, qreal strength) {
        QLinearGradient g(x, 0, toRight ? x + width : x - width, 0);
        g.setColorAt(0, QColor(0, 0, 0, int(110 * strength)));
        g.setColorAt(1, QColor(0, 0, 0, 0));
        p.fillRect(QRectF(toRight ? x : x - width, 0, width, h), g);
    };
    // Right to left, the next page comes in from the left.
    const qreal dir = rtl ? -1.0 : 1.0;
    if (style == QLatin1String("slide")) {
        draw(p, QPointF(-s * w * dir, 0), a);
        draw(p, QPointF((1 - s) * w * dir, 0), b);
    } else if (style == QLatin1String("cover")) {
        draw(p, QPointF(0, 0), a);
        const qreal x = (1 - s) * w * dir;
        if (rtl) shadow(x + w, 24, true, 1.0 - s * 0.5);
        else shadow(x, 24, false, 1.0 - s * 0.5);
        draw(p, QPointF(x, 0), b);
    } else if (style == QLatin1String("fade")) {
        draw(p, QPointF(0, 0), a);
        p.setOpacity(s);
        draw(p, QPointF(0, 0), b);
        p.setOpacity(1);
    } else if (style == QLatin1String("uncover")) {
        // Cover the other way: the old page slides away, the new one lies under it.
        draw(p, QPointF(0, 0), b);
        const qreal x = -s * w * dir;
        if (rtl) shadow(x, 24, false, 1.0 - s * 0.5);
        else shadow(x + w, 24, true, 1.0 - s * 0.5);
        draw(p, QPointF(x, 0), a);
    } else if (style == QLatin1String("flip") || style == QLatin1String("card")) {
        // In perspective, about an upright axis: a hardcover page swings over on its spine (the left edge; the
        // right one right to left) and the new page lies under it; a card turns over about its middle, the new
        // page on its back.
        const bool card = style == QLatin1String("card");
        auto turned = [&](const Picture& pm, qreal axisX, qreal degrees, qreal darken) {
            QTransform tr;
            tr.translate(axisX, h / 2);
            tr.rotate(degrees, Qt::YAxis, qMax<qreal>(1024, w * 1.6));
            tr.translate(-axisX, -h / 2);
            p.save();
            p.setTransform(tr, true);
            draw(p, QPointF(0, 0), pm);
            if (darken > 0) p.fillRect(QRectF(0, 0, w, h), QColor(0, 0, 0, int(140 * darken)));
            p.restore();
        };
        if (card) {
            // The first half the old page turns edge-on, the second the new one turns to face the reader.
            const qreal deg = s < 0.5 ? s * 180 : s * 180 - 180;
            const qreal darken = s < 0.5 ? s * 2 * 0.5 : (1 - s) * 2 * 0.5;
            turned(s < 0.5 ? a : b, w / 2, deg * dir, darken);
        } else {
            draw(p, QPointF(0, 0), b);
            shadow(rtl ? w * s : w * (1 - s), qMin<qreal>(64, w * 0.2), rtl, 1.0 - s);
            turned(a, rtl ? w : 0, -s * 90 * dir, s * 0.6);
        }
    } else {   // curl: the page folds back from the right edge, its back lying over the page that is left
        const qreal x = w * (1 - s);              // the fold
        const qreal flap = qMin(w - x, x);        // the folded part, lying left of the fold
        draw(p, QPointF(0, 0), b);
        shadow(x, qMin<qreal>(48, w - x), true, 1.0 - s);
        p.save();
        p.setClipRect(QRectF(0, 0, x, h));
        draw(p, QPointF(0, 0), a);
        p.restore();
        if (flap > 1) {
            const QRectF back(x - flap, 0, flap, h);
            shadow(x - flap, qMin<qreal>(24, x - flap), false, 0.6);
            p.fillRect(back, bg);
            // The page's own print showing through its back, mirrored.
            p.save();
            p.setClipRect(back);
            p.setOpacity(0.06);
            p.translate(2 * x, 0);
            p.scale(-1, 1);
            draw(p, QPointF(0, 0), a);
            p.restore();
            QLinearGradient g(x - flap, 0, x, 0);
            g.setColorAt(0, QColor(255, 255, 255, dark ? 18 : 60));
            g.setColorAt(0.7, QColor(0, 0, 0, 0));
            g.setColorAt(1, QColor(0, 0, 0, 70));
            p.fillRect(back, g);
            p.setPen(QPen(line, 1));
            p.drawLine(QPointF(x - flap, 0), QPointF(x - flap, h));
        }
    }
}

}  // namespace layout

#include "FlipAnimation.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>

namespace flip {

namespace {

class Overlay : public QWidget {
public:
    Overlay(QWidget* card, QPixmap before, QPixmap after, QColor bg, int radius)
        : QWidget(card), m_before(std::move(before)), m_after(std::move(after)), m_bg(bg), m_radius(radius)
    {
        setObjectName(QStringLiteral("FlipOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(card->rect());
        raise();
        show();
    }
    void setPhase(double t)
    {
        m_t = t;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        // The card's own shape and color hide the live widgets while it turns.
        QPainterPath shape;
        shape.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_radius, m_radius);
        p.fillPath(shape, m_bg);
        const bool first = m_t < 0.5;
        const QPixmap& pm = first ? m_before : m_after;
        const double s = first ? 1.0 - 2.0 * m_t : 2.0 * m_t - 1.0;   // 1 -> 0, then 0 -> 1
        const QSizeF size = pm.deviceIndependentSize();
        const double w = size.width() * s;
        if (w < 1) return;
        p.drawPixmap(QRectF((width() - w) / 2.0, 0, w, size.height()), pm, QRectF(QPointF(0, 0), pm.size()));
    }

private:
    QPixmap m_before, m_after;
    QColor m_bg;
    int m_radius;
    double m_t = 0;
};

}  // namespace

void turn(QWidget* card, const std::function<void()>& change, const QColor& background, int radius, int ms)
{
    if (!card) return;
    if (ms <= 0 || !card->isVisible()) {
        change();
        return;
    }
    for (QWidget* old : card->findChildren<QWidget*>(QStringLiteral("FlipOverlay"), Qt::FindDirectChildrenOnly)) old->deleteLater();
    const QPixmap before = card->grab();
    change();
    const QPixmap after = card->grab();
    auto* overlay = new Overlay(card, before, after, background, radius);
    auto* anim = new QVariantAnimation(overlay);
    anim->setDuration(ms);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::InOutSine);
    QObject::connect(anim, &QVariantAnimation::valueChanged, overlay, [overlay](const QVariant& v) { overlay->setPhase(v.toDouble()); });
    QObject::connect(anim, &QVariantAnimation::finished, overlay, &QObject::deleteLater);
    anim->start();
}

bool isTurning(const QWidget* card)
{
    return card && !card->findChildren<QWidget*>(QStringLiteral("FlipOverlay"), Qt::FindDirectChildrenOnly).isEmpty();
}

}  // namespace flip

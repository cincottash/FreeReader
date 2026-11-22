#include "FooterBar.h"

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace {
constexpr int kTrackHit = 10;   // the clickable band at the top (the visible track is 3 px)
}

FooterBar::FooterBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("Footer"));
    setAttribute(Qt::WA_StyledBackground);
    setMouseTracking(true);
    setToolTip(tr("Click the bar to go to that point of the book."));
}

void FooterBar::setTheme(const Theme& t)
{
    m_theme = t;
    update();
}

void FooterBar::setProgress(double fraction, const QStringList& stats)
{
    m_fraction = qBound(0.0, fraction, 1.0);
    m_stats = stats;
    update();
}

void FooterBar::setMarks(const std::vector<double>& fractions)
{
    m_marks = fractions;
    update();
}

QSize FooterBar::sizeHint() const { return {400, fontMetrics().height() + 24}; }

void FooterBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), m_theme.surface);
    p.fillRect(QRect(0, 0, width(), 1), m_theme.line);
    const int h = m_hover >= 0 ? 5 : 3;
    p.fillRect(QRect(0, 0, width(), h), m_theme.line);
    p.fillRect(QRectF(0, 0, width() * m_fraction, h), m_theme.accent);
    QColor mark = m_theme.muted;
    mark.setAlphaF(0.7f);
    for (double f : m_marks) p.fillRect(QRectF(width() * f, 0, 1, h + 3), mark);
    if (m_hover >= 0) p.fillRect(QRectF(width() * m_hover - 1, 0, 2, h + 4), m_theme.fg);

    p.setPen(m_theme.muted);
    const QFontMetrics fm(font());
    const int gap = 20;
    int total = 0;
    for (const QString& s : m_stats) total += fm.horizontalAdvance(s);
    total += gap * qMax(0, int(m_stats.size()) - 1);
    int x = (width() - total) / 2;
    const int y = 3 + (height() - 3 + fm.ascent() - fm.descent()) / 2;
    for (const QString& s : m_stats) {
        p.drawText(x, y, s);
        x += fm.horizontalAdvance(s) + gap;
    }
}

void FooterBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && e->position().y() <= kTrackHit && width() > 0)
        emit jumpRequested(qBound(0.0, e->position().x() / width(), 1.0));
}

void FooterBar::mouseMoveEvent(QMouseEvent* e)
{
    const double h = e->position().y() <= kTrackHit && width() > 0 ? qBound(0.0, e->position().x() / width(), 1.0) : -1;
    if (h != m_hover) {
        m_hover = h;
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (h >= 0) QToolTip::showText(e->globalPosition().toPoint(), QLocale().toString(qRound(h * 100)) + QStringLiteral("%"), this);
        update();
    }
}

void FooterBar::leaveEvent(QEvent*)
{
    m_hover = -1;
    update();
}

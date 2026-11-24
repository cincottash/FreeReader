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

void FooterBar::setClickableStat(int index, const QString& tooltip)
{
    m_clickable = index;
    m_clickableTip = tooltip;
}

QList<QPair<int, QString>> FooterBar::shown() const
{
    // What fits: in a narrow window the middle items go first (the percentage), then the first; the last one left is
    // shortened. Nothing is drawn past the edges.
    QList<QPair<int, QString>> v;
    for (int i = 0; i < m_stats.size(); ++i) v << qMakePair(i, m_stats[i]);
    const QFontMetrics fm(font());
    const int gap = 20, room = qMax(0, width() - 24);
    auto total = [&] {
        int t = gap * qMax(0, int(v.size()) - 1);
        for (const auto& s : v) t += fm.horizontalAdvance(s.second);
        return t;
    };
    while (v.size() > 2 && total() > room) v.removeAt(v.size() / 2);
    while (v.size() > 1 && total() > room) v.removeFirst();
    if (v.size() == 1 && fm.horizontalAdvance(v[0].second) > room) v[0].second = fm.elidedText(v[0].second, Qt::ElideRight, room);
    return v;
}

QStringList FooterBar::shownStats() const
{
    QStringList out;
    for (const auto& s : shown()) out << s.second;
    return out;
}

QRect FooterBar::statRect(int index) const
{
    const auto v = shown();
    const QFontMetrics fm(font());
    const int gap = 20;
    int total = gap * qMax(0, int(v.size()) - 1);
    for (const auto& s : v) total += fm.horizontalAdvance(s.second);
    int x = (width() - total) / 2;
    for (const auto& s : v) {
        if (s.first == index) return QRect(x - 6, kTrackHit, fm.horizontalAdvance(s.second) + 12, height() - kTrackHit);
        x += fm.horizontalAdvance(s.second) + gap;
    }
    return {};
}

int FooterBar::statAt(const QPointF& pos) const
{
    for (int i = 0; i < m_stats.size(); ++i)
        if (statRect(i).contains(pos.toPoint())) return i;
    return -1;
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
    const int y = 3 + (height() - 3 + fm.ascent() - fm.descent()) / 2;
    for (const auto& s : shown()) p.drawText(statRect(s.first).left() + 6, y, s.second);
}

void FooterBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || width() <= 0) return;
    if (e->position().y() <= kTrackHit) {
        emit jumpRequested(qBound(0.0, e->position().x() / width(), 1.0));
        return;
    }
    const int i = statAt(e->position());
    if (i >= 0 && i == m_clickable) emit statClicked(i);
}

void FooterBar::mouseMoveEvent(QMouseEvent* e)
{
    const double h = e->position().y() <= kTrackHit && width() > 0 ? qBound(0.0, e->position().x() / width(), 1.0) : -1;
    const bool onStat = h < 0 && m_clickable >= 0 && statAt(e->position()) == m_clickable;
    setToolTip(onStat ? m_clickableTip : tr("Click the bar to go to that point of the book."));
    if (h < 0) setCursor(onStat ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (h != m_hover) {
        m_hover = h;
        setCursor(h >= 0 || onStat ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (h >= 0) QToolTip::showText(e->globalPosition().toPoint(), QLocale().toString(qRound(h * 100)) + QStringLiteral("%"), this);
        update();
    }
}

void FooterBar::leaveEvent(QEvent*)
{
    m_hover = -1;
    update();
}

#include "Icons.h"

#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace icons {

namespace {

const QHash<QString, QString>& shapes()
{
    static const QHash<QString, QString> s = {
        {"back", "<path d='M15 5l-7 7 7 7'/>"},
        {"search", "<circle cx='11' cy='11' r='6.5'/><path d='M16 16l4.5 4.5'/>"},
        {"toc", "<path d='M8 6h12M8 12h12M8 18h12M4 6h.01M4 12h.01M4 18h.01'/>"},
        {"bookmark", "<path d='M7 4h10v16l-5-4-5 4z'/>"},
        {"panel", "<rect x='3.5' y='4.5' width='17' height='15' rx='2.5'/><path d='M14.5 4.5v15'/>"},
        {"commands", "<rect x='3.5' y='5.5' width='17' height='13' rx='2.5'/><path d='M7 10l3 2-3 2M12 14h5'/>"},
        {"fullscreen", "<path d='M4 9V5.5A1.5 1.5 0 015.5 4H9M15 4h3.5A1.5 1.5 0 0120 5.5V9M20 15v3.5a1.5 1.5 0 01-1.5 1.5H15M9 20H5.5A1.5 1.5 0 014 18.5V15'/>"},
        {"exitFullscreen", "<path d='M9 4v3.5A1.5 1.5 0 017.5 9H4M20 9h-3.5A1.5 1.5 0 0115 7.5V4M15 20v-3.5a1.5 1.5 0 011.5-1.5H20M4 15h3.5A1.5 1.5 0 019 16.5V20'/>"},
        {"chevronLeft", "<path d='M14.5 6l-6 6 6 6'/>"},
        {"chevronRight", "<path d='M9.5 6l6 6-6 6'/>"},
        {"close", "<path d='M6 6l12 12M18 6L6 18'/>"},
        {"open", "<path d='M4 7.5A1.5 1.5 0 015.5 6H10l2 2h6.5A1.5 1.5 0 0120 9.5v8a1.5 1.5 0 01-1.5 1.5h-13A1.5 1.5 0 014 17.5z'/>"},
        {"more", "<circle cx='6' cy='12' r='1.2'/><circle cx='12' cy='12' r='1.2'/><circle cx='18' cy='12' r='1.2'/>"},
        {"theme", "<circle cx='12' cy='12' r='7.5'/><path d='M12 4.5a7.5 7.5 0 010 15z' fill='COLOR'/>"},
        {"grid", "<rect x='4' y='4' width='6.5' height='6.5' rx='1.2'/><rect x='13.5' y='4' width='6.5' height='6.5' rx='1.2'/><rect x='4' y='13.5' width='6.5' height='6.5' rx='1.2'/><rect x='13.5' y='13.5' width='6.5' height='6.5' rx='1.2'/>"},
        {"list", "<path d='M8 6h12M8 12h12M8 18h12'/><circle cx='4.5' cy='6' r='0.8'/><circle cx='4.5' cy='12' r='0.8'/><circle cx='4.5' cy='18' r='0.8'/>"},
        {"speaker", "<path d='M4 9.5v5h3.5L12 18V6L7.5 9.5z'/><path d='M15.5 9a4 4 0 010 6M18 6.5a7.5 7.5 0 010 11'/>"},
        {"book", "<path d='M5 4.5h9.5a2 2 0 012 2V20H7a2 2 0 01-2-2z'/><path d='M5 18a2 2 0 012-2h9.5M19 7v13'/>"},
        // Two pages side by side (an open book), and flashcards (two cards, one behind the other).
        {"spread", "<rect x='2.5' y='5' width='8.5' height='14' rx='1.2'/><rect x='13' y='5' width='8.5' height='14' rx='1.2'/><path d='M5 9h3.5M5 12h3.5M5 15h3.5M15.5 9H19M15.5 12H19M15.5 15H19'/>"},
        {"cards", "<rect x='3.5' y='7.5' width='13' height='12' rx='2'/><path d='M7.5 4.5h11a2 2 0 012 2v9'/><path d='M7.5 13.5h5'/>"},
    };
    return s;
}

}  // namespace

QIcon icon(const char* name, const QColor& color, int size)
{
    const QString body = shapes().value(QString::fromLatin1(name));
    const QString svg = QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%1' "
                                       "stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'>%2</svg>")
                            .arg(color.name(), QString(body).replace(QLatin1String("COLOR"), color.name()));
    QSvgRenderer r(svg.toUtf8());
    QIcon ic;
    for (qreal dpr : {1.0, 1.5, 2.0}) {
        QPixmap pm(QSize(qRound(size * dpr), qRound(size * dpr)));
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        r.render(&p);
        p.end();
        pm.setDevicePixelRatio(dpr);
        ic.addPixmap(pm);
    }
    return ic;
}

QIcon dot(const QColor& color, const QColor& ring)
{
    QPixmap pm(32, 32);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(ring.isValid() ? QPen(ring, 1.6) : Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(2, 2, 12, 12));
    p.end();
    return QIcon(pm);
}

}  // namespace icons

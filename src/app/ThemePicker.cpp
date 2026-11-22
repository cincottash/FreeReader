#include "ThemePicker.h"

#include "Theme.h"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kKeyRole = Qt::UserRole;

// A small round swatch: the page color with the accent as a dot.
QIcon swatch(const Theme& t)
{
    QIcon ic;
    for (qreal dpr : {1.0, 2.0}) {
        QPixmap pm(QSize(18, 18) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(t.line, 1));
        p.setBrush(t.bg);
        p.drawEllipse(QRectF(1, 1, 16, 16));
        p.setPen(Qt::NoPen);
        p.setBrush(t.accent);
        p.drawEllipse(QRectF(9, 9, 6, 6));
        p.end();
        ic.addPixmap(pm);
    }
    return ic;
}

}  // namespace

ThemePicker::ThemePicker(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Pop"));
    setAttribute(Qt::WA_StyledBackground);
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(32);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(0, 0, 0, 90));
    setGraphicsEffect(shadow);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    auto* title = new QLabel(tr("THEMES"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    title->setStyleSheet(QStringLiteral("padding: 6px 8px 4px 8px;"));
    lay->addWidget(title);
    m_list = new QListWidget;
    m_list->setMouseTracking(true);
    m_list->setIconSize(QSize(18, 18));
    m_list->setProperty("testId", "themeList");
    lay->addWidget(m_list, 1);
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(90);   // themes the pointer only passes over aren't shown
    connect(&m_previewTimer, &QTimer::timeout, this, [this] { emit previewRequested(m_pending); });
    connect(m_list, &QListWidget::itemEntered, this, &ThemePicker::hoverItem);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        const QString key = it->data(kKeyRole).toString();
        if (key.isEmpty()) return;   // a group's name
        m_previewTimer.stop();
        m_pending.clear();
        emit chosen(key);
        hide();
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (isVisible() && now && now != this && !isAncestorOf(now)) hide();
    });
    hide();
}

void ThemePicker::fill(const QString& currentKey)
{
    m_list->clear();
    QListWidgetItem* current = nullptr;
    for (const QString& g : Theme::groups()) {
        auto* head = new QListWidgetItem(g, m_list);
        head->setFlags(Qt::ItemIsEnabled);   // not selectable: pointing at it shows the saved theme
        QFont f = head->font();
        f.setBold(true);
        head->setFont(f);
        QList<const ThemeDef*> defs;
        for (const ThemeDef& d : themeTable())
            if (d.group == g) defs << &d;
        std::sort(defs.begin(), defs.end(), [](auto* a, auto* b) { return a->name.compare(b->name, Qt::CaseInsensitive) < 0; });
        for (const ThemeDef* d : defs) {
            auto* it = new QListWidgetItem(swatch(Theme::fromDef(*d)), QStringLiteral("   ") + d->name, m_list);
            it->setData(kKeyRole, d->key);
            if (d->key == currentKey) current = it;
        }
    }
    auto* system = new QListWidgetItem(tr("Match Operating System"), m_list);
    system->setData(kKeyRole, QStringLiteral("system"));
    system->setToolTip(tr("Midnight when the system uses dark mode, Paper when it uses light mode."));
    if (currentKey == QLatin1String("system")) current = system;
    if (current) {
        m_list->setCurrentItem(current);
        m_list->scrollToItem(current, QAbstractItemView::PositionAtCenter);
    }
}

void ThemePicker::showFor(const QString& currentKey, const QPoint& topRight)
{
    fill(currentKey);
    QWidget* p = parentWidget();
    const int w = 300, h = qMin(560, p->height() - topRight.y() - 16);
    setGeometry(qBound(8, topRight.x() - w, p->width() - w - 8), topRight.y(), w, h);
    show();
    raise();
    m_list->setFocus();
}

void ThemePicker::hoverItem(QListWidgetItem* it)
{
    // Only a theme's own entry previews it; anything else in the list shows the saved theme.
    const QString key = it ? it->data(kKeyRole).toString() : QString();
    if (key == m_pending) return;
    m_pending = key;
    m_previewTimer.start();
}

void ThemePicker::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (QListWidgetItem* it = m_list->currentItem()) emit m_list->itemClicked(it);
        return;
    }
    QFrame::keyPressEvent(e);
}

void ThemePicker::hideEvent(QHideEvent* e)
{
    m_previewTimer.stop();
    m_pending.clear();
    emit previewRequested(QString());   // closing without a choice restores the saved theme
    QFrame::hideEvent(e);
}

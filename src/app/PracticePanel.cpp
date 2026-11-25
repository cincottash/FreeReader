#include "PracticePanel.h"

#include "Icons.h"
#include "PracticeView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

// The panel's left edge: dragged left, the panel grows; right, it shrinks (the owner's request, 2026-11-25). While
// it is dragged (`moving`) the panel follows the mouse and the book's page keeps its layout; on release (`done`)
// the page is laid out once for its new width (laid out at every step it jumped about, 2026-11-25).
class PanelGrip : public QWidget {
public:
    PanelGrip(QWidget* panel, std::function<void(int)> moving, std::function<void(int)> done)
        : QWidget(panel), m_panel(panel), m_moving(std::move(moving)), m_done(std::move(done))
    {
        setCursor(Qt::SizeHorCursor);
        setToolTip(PracticePanel::tr("Drag to make the panel wider or narrower"));
        setProperty("testId", "practicePanelGrip");
    }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        m_startX = e->globalPosition().x();
        m_startWidth = m_panel->width();
        m_dragging = true;
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_dragging) m_moving(m_startWidth + int(m_startX - e->globalPosition().x()));
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (!m_dragging) return;
        m_dragging = false;
        m_done(m_startWidth + int(m_startX - e->globalPosition().x()));
    }

private:
    QWidget* m_panel;
    std::function<void(int)> m_moving, m_done;
    qreal m_startX = 0;
    int m_startWidth = 0;
    bool m_dragging = false;
};

namespace {

QToolButton* headerButton(const QString& tip, const char* id)
{
    auto* b = new QToolButton;
    b->setObjectName(QStringLiteral("Icon"));
    b->setFixedSize(32, 32);
    b->setIconSize(QSize(18, 18));
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setProperty("testId", id);
    return b;
}

}  // namespace

PracticePanel::PracticePanel(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Panel"));
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral("#Panel { border-right: 0; }"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 8, 0);
    top->setSpacing(2);
    auto* title = new QLabel(tr("PRACTICE"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    top->addWidget(title, 1);
    m_full = headerButton(tr("Maximize"), "practicePanelFull");
    m_close = headerButton(tr("Close (Ctrl+P)"), "practicePanelClose");
    top->addWidget(m_full);
    top->addWidget(m_close);
    lay->addLayout(top);
    connect(m_full, &QToolButton::clicked, this, &PracticePanel::fullWindowRequested);
    connect(m_close, &QToolButton::clicked, this, &PracticePanel::closeRequested);
    m_grip = new PanelGrip(this, [this](int width) { emit widthDragging(width); }, [this](int width) { emit widthDragged(width); });
}

void PracticePanel::resizeEvent(QResizeEvent* e)
{
    QFrame::resizeEvent(e);
    m_grip->setGeometry(0, 0, 6, height());
    m_grip->raise();
}

void PracticePanel::setView(PracticeView* view)
{
    m_view = view;
    view->setParent(this);
    view->setCompact(true);
    layout()->addWidget(view);
    m_grip->raise();   // above the view, at the left edge
}

void PracticePanel::setTheme(const Theme& t)
{
    // A line between the page and the workspace.
    setStyleSheet(QStringLiteral("#Panel { border: 0; border-left: 1px solid %1; }").arg(t.line.name()));
    m_fg = t.fg;
    m_full->setIcon(icons::icon(m_maximized ? "collapse" : "expand", t.fg));
    m_close->setIcon(icons::icon("close", t.fg));
    if (m_view) m_view->setTheme(t);
}

int PracticePanel::narrowest() const
{
    // 360 px, or more when its contents need more (the Word List's buttons, longer in Spanish): at 360 px "Show in
    // Book" was cut off (2026-11-25).
    return qMax(360, layout()->minimumSize().width());
}

void PracticePanel::setMaximized(bool on)
{
    // The same button, in the same place, makes it small again (the owner's request, 2026-11-25).
    m_maximized = on;
    m_full->setIcon(icons::icon(on ? "collapse" : "expand", m_fg));
    m_full->setToolTip(on ? tr("Restore") : tr("Maximize"));
    m_grip->setVisible(!on);   // nothing to drag over the whole width
}

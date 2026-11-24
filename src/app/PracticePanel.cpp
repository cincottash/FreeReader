#include "PracticePanel.h"

#include "Icons.h"
#include "PracticeView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

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
    m_full = headerButton(tr("Open Practice in the Full Window"), "practicePanelFull");
    m_close = headerButton(tr("Close (Ctrl+P)"), "practicePanelClose");
    top->addWidget(m_full);
    top->addWidget(m_close);
    lay->addLayout(top);
    connect(m_full, &QToolButton::clicked, this, &PracticePanel::fullWindowRequested);
    connect(m_close, &QToolButton::clicked, this, &PracticePanel::closeRequested);
}

void PracticePanel::setView(PracticeView* view)
{
    m_view = view;
    view->setParent(this);
    view->setCompact(true);
    layout()->addWidget(view);
}

void PracticePanel::setTheme(const Theme& t)
{
    // A line between the page and the workspace.
    setStyleSheet(QStringLiteral("#Panel { border: 0; border-left: 1px solid %1; }").arg(t.line.name()));
    m_full->setIcon(icons::icon("fullscreen", t.fg));
    m_close->setIcon(icons::icon("close", t.fg));
    if (m_view) m_view->setTheme(t);
}

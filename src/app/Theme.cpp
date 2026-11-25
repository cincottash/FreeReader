#include "Theme.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

QPalette themePalette(const Theme& t)
{
    QPalette p;
    p.setColor(QPalette::Window, t.surface);
    p.setColor(QPalette::WindowText, t.fg);
    p.setColor(QPalette::Base, t.input);
    p.setColor(QPalette::AlternateBase, t.surface);
    p.setColor(QPalette::Text, t.fg);
    p.setColor(QPalette::Button, t.surface);
    p.setColor(QPalette::ButtonText, t.fg);
    p.setColor(QPalette::Highlight, t.accent);
    p.setColor(QPalette::HighlightedText, t.accentFg);
    p.setColor(QPalette::ToolTipBase, t.pop);
    p.setColor(QPalette::ToolTipText, t.fg);
    p.setColor(QPalette::PlaceholderText, t.muted);
    p.setColor(QPalette::Link, t.accent);
    p.setColor(QPalette::Disabled, QPalette::Text, t.muted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, t.muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.muted);
    return p;
}

QString themeStyleSheet(const Theme& t)
{
    auto c = [](const QColor& col) { return col.name(QColor::HexArgb); };
    QString css = QStringLiteral(R"(
QWidget { color: @fg; }
QToolTip { background: @pop; color: @fg; border: 1px solid @line; padding: 4px 6px; border-radius: 6px; }
#Bar { background: @surface; border-bottom: 1px solid @line; }
#Footer { background: @surface; border-top: 1px solid @line; }
#Panel { background: @surface; border-right: 1px solid @line; }
#PanelTitle { color: @muted; font-size: 11px; font-weight: 600; letter-spacing: 1px; padding: 14px 20px 6px 20px; }
#Pop { background: @pop; border: 1px solid @line; border-radius: 14px; }
#Muted { color: @muted; }
#StartView, #Page { background: @bg; }
#Title { font-weight: 600; }
QToolButton#Icon { border: 0; border-radius: 10px; background: transparent; }
QToolButton#Icon:hover { background: @hover; }
QToolButton#Icon:checked { background: @hl; }
QPushButton#Primary { background: @accent; color: @accentFg; border: 0; border-radius: 10px; padding: 10px 18px; font-weight: 600; }
QPushButton#Primary:hover { background: @accentHover; }
QPushButton#Chip { background: @pop; color: @fg; border: 1px solid @line; border-radius: 16px; padding: 6px 14px; }
QPushButton#Chip:hover { border-color: @accent; }
QPushButton#Chip:checked { background: @hl; border-color: @accent; }
QToolButton#Practice { background: transparent; color: @fg; border: 1px solid @accent; border-radius: 10px; padding: 4px 12px 4px 8px; font-weight: 600; }
QToolButton#Practice:hover { background: @hover; }
QToolButton#Practice:checked { background: @hl; }
QPushButton#BigPractice { background: @accent; color: @accentFg; border: 0; border-radius: 8px; padding: 0 12px; text-align: left; font-weight: 600; }
QPushButton#BigPractice:hover { background: @accentHover; }
QPushButton#BigPractice QLabel { color: @accentFg; background: transparent; }
QPushButton#Link { background: transparent; color: @accent; border: 0; padding: 4px 0; text-align: left; }
QPushButton#Link:hover { text-decoration: underline; }
QLineEdit { background: @input; border: 1px solid @line; border-radius: 8px; padding: 8px 10px; selection-background-color: @accent; selection-color: @accentFg; }
QLineEdit:focus { border-color: @accent; }
QAbstractItemView QLineEdit { padding: 0 4px; border-radius: 4px; border: 1px solid @accent; background: @input; }
QTreeView, QListView { background: transparent; border: 0; outline: 0; }
QListView#FramedList { background: @input; border: 1px solid @line; border-radius: 8px; padding: 4px; }
QTreeView::item, QListView::item { padding: 6px 4px; border-radius: 6px; }
QTreeView::item:hover, QListView::item:hover { background: @hover; }
QTreeView::item:selected, QListView::item:selected { background: @hl; color: @fg; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @line; border-radius: 4px; min-height: 30px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QScrollBar:horizontal { height: 0; }
QMessageBox, QDialog { background: @surface; }
QPushButton { background: @pop; border: 1px solid @line; border-radius: 8px; padding: 6px 14px; }
QPushButton:hover { border-color: @accent; }
QComboBox { background: @input; border: 1px solid @line; border-radius: 8px; padding: 6px 28px 6px 10px; }
QComboBox:hover { border-color: @accent; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 26px; border: 0; background: transparent; }
QComboBox::down-arrow { image: url(@chevron); width: 14px; height: 14px; }
QComboBox QAbstractItemView { background: @pop; border: 1px solid @line; selection-background-color: @hl; selection-color: @fg; }
QSlider::groove:horizontal { height: 4px; background: @line; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @fg; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid @muted; border-radius: 4px; background: @input; }
QCheckBox::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(@check); }
QCheckBox::indicator:disabled { border-color: @line; background: transparent; }
QFrame#Rule { background: @line; border: 0; }
QPushButton#Suggested { border: 2px solid @accent; font-weight: 600; padding: 5px 13px; }
QTabWidget::pane { border: 1px solid @line; border-radius: 8px; top: -1px; background: @surface; }
QTabBar::tab { background: transparent; color: @muted; padding: 8px 14px; border: 0; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: @fg; border-bottom-color: @accent; }
QTabBar::tab:hover { color: @fg; }
QMenuBar { background: @surface; border-bottom: 1px solid @line; padding: 2px 6px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 6px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: @hover; }
QMenu { background: @pop; border: 1px solid @line; padding: 4px; }
QMenu::item { padding: 6px 28px 6px 24px; border-radius: 6px; }
QMenu::item:selected { background: @hl; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @line; margin: 4px 8px; }
QMenu::indicator { width: 14px; height: 14px; left: 6px; }
)");
    QColor hover = t.fg;
    hover.setAlphaF(0.07f);
    css.replace(QLatin1String("@accentHover"), c(t.accentHover))
        .replace(QLatin1String("@accentFg"), c(t.accentFg))
        .replace(QLatin1String("@accent"), c(t.accent))
        .replace(QLatin1String("@surface"), c(t.surface))
        .replace(QLatin1String("@pop"), c(t.pop))
        .replace(QLatin1String("@input"), c(t.input))
        .replace(QLatin1String("@fg"), c(t.fg))
        .replace(QLatin1String("@muted"), c(t.muted))
        .replace(QLatin1String("@line"), c(t.line))
        .replace(QLatin1String("@hover"), c(hover))
        .replace(QLatin1String("@hl"), c(t.hl))
        .replace(QLatin1String("@bg"), c(t.bg))
        // A check mark that shows on the accent color.
        .replace(QLatin1String("@check"), t.accentFg.lightnessF() > 0.5f ? QStringLiteral(":/check-light.svg") : QStringLiteral(":/check-dark.svg"))
        // A drop-down list's arrow in the text's color, on the box itself: no square of its own (the owner's report,
        // 2026-11-25: the default one looked like a box squeezed into the rounded corner).
        .replace(QLatin1String("@chevron"), t.fg.lightnessF() > 0.5f ? QStringLiteral(":/chevron-light.svg") : QStringLiteral(":/chevron-dark.svg"));
    return css;
}

void applyTheme(QApplication& app, const Theme& t)
{
    if (!app.style() || app.style()->name().compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(themePalette(t));
    app.setStyleSheet(themeStyleSheet(t));
}

void applyThemeTo(QWidget* w, const Theme& t)
{
    w->setPalette(themePalette(t));
    w->setStyleSheet(themeStyleSheet(t));
}

void clearThemeFrom(QWidget* w)
{
    if (w->styleSheet().isEmpty()) return;
    w->setStyleSheet(QString());
    w->setPalette(QPalette());
}

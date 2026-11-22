#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

Theme Theme::midnight()
{
    Theme t;
    t.key = QStringLiteral("midnight");
    t.name = QStringLiteral("Midnight");
    t.bg = QColor(0x0f, 0x11, 0x15);
    t.surface = QColor(0x16, 0x19, 0x20);
    t.pop = QColor(0x1d, 0x21, 0x2a);
    t.fg = QColor(0xe4, 0xe1, 0xd8);
    t.muted = QColor(0xa0, 0xa6, 0xb3);
    t.line = QColor(0x2a, 0x2f, 0x3a);
    t.accent = QColor(0xe0, 0xa8, 0x4a);
    t.accentFg = QColor(0x1a, 0x14, 0x08);
    t.hl = QColor(224, 168, 74, 71);
    t.sel = QColor(110, 160, 255, 97);
    t.dark = true;
    return t;
}

void applyTheme(QApplication& app, const Theme& t)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    p.setColor(QPalette::Window, t.surface);
    p.setColor(QPalette::WindowText, t.fg);
    p.setColor(QPalette::Base, t.bg);
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
    app.setPalette(p);

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
#StartView { background: @bg; }
#Title { font-weight: 600; }
QToolButton#Icon { border: 0; border-radius: 10px; background: transparent; }
QToolButton#Icon:hover { background: @hover; }
QToolButton#Icon:checked { background: @hl; }
QPushButton#Primary { background: @accent; color: @accentFg; border: 0; border-radius: 10px; padding: 10px 18px; font-weight: 600; }
QPushButton#Primary:hover { background: @accentHover; }
QPushButton#Chip { background: @pop; color: @fg; border: 1px solid @line; border-radius: 16px; padding: 6px 14px; }
QPushButton#Chip:hover { border-color: @accent; }
QPushButton#Link { background: transparent; color: @accent; border: 0; padding: 4px 0; text-align: left; }
QPushButton#Link:hover { text-decoration: underline; }
QLineEdit { background: @bg; border: 1px solid @line; border-radius: 8px; padding: 8px 10px; selection-background-color: @accent; selection-color: @accentFg; }
QLineEdit:focus { border-color: @accent; }
QTreeView, QListView { background: transparent; border: 0; outline: 0; }
QTreeView::item, QListView::item { padding: 6px 4px; border-radius: 6px; }
QTreeView::item:hover, QListView::item:hover { background: @hover; }
QTreeView::item:selected, QListView::item:selected { background: @hl; color: @fg; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @line; border-radius: 4px; min-height: 30px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QScrollBar:horizontal { height: 0; }
QMessageBox { background: @surface; }
QDialog { background: @surface; }
QPushButton { background: @pop; border: 1px solid @line; border-radius: 8px; padding: 6px 14px; }
QPushButton:hover { border-color: @accent; }
QCheckBox::indicator { width: 16px; height: 16px; }
)");
    QColor hover = t.fg;
    hover.setAlphaF(0.07f);
    css.replace(QLatin1String("@accentHover"), c(t.accent.lighter(110)))
        .replace(QLatin1String("@accentFg"), c(t.accentFg))
        .replace(QLatin1String("@accent"), c(t.accent))
        .replace(QLatin1String("@surface"), c(t.surface))
        .replace(QLatin1String("@pop"), c(t.pop))
        .replace(QLatin1String("@fg"), c(t.fg))
        .replace(QLatin1String("@muted"), c(t.muted))
        .replace(QLatin1String("@line"), c(t.line))
        .replace(QLatin1String("@hover"), c(hover))
        .replace(QLatin1String("@hl"), c(t.hl))
        .replace(QLatin1String("@bg"), c(t.bg));
    app.setStyleSheet(css);
}

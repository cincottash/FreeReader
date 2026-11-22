#include "Theme.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace {

QColor color(const ThemeDef& d, const char* key, const QColor& fallback = QColor())
{
    const QString v = d.colors.value(QString::fromLatin1(key));
    return v.isEmpty() ? fallback : QColor(v);
}

QColor withAlpha(QColor c, float a)
{
    c.setAlphaF(a);
    return c;
}

}  // namespace

Theme Theme::fromDef(const ThemeDef& d)
{
    Theme t;
    t.key = d.key;
    t.name = d.name;
    t.group = d.group;
    t.bg = color(d, "bg");
    t.surface = color(d, "card", t.bg);
    t.dark = t.bg.lightnessF() < 0.5f;
    t.pop = color(d, "pop", t.dark ? t.surface.lighter(112) : t.surface);
    t.input = color(d, "input", t.bg);
    t.fg = color(d, "text");
    t.muted = color(d, "muted");
    t.line = color(d, "border", t.muted);
    t.accent = color(d, "accent");
    t.accentHover = color(d, "accent_hover", t.accent);
    t.accentFg = color(d, "accent_text", t.dark ? QColor(Qt::black) : QColor(Qt::white));
    t.hl = withAlpha(t.accent, t.dark ? 0.28f : 0.30f);
    t.sel = withAlpha(color(d, "sel", t.accentHover), t.dark ? 0.38f : 0.28f);
    return t;
}

Theme Theme::midnight()
{
    for (const ThemeDef& d : themeTable())
        if (d.key == QLatin1String("midnight")) return fromDef(d);
    return fromDef(themeTable().first());
}

bool Theme::systemIsDark()
{
    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}

Theme Theme::forChoice(const QString& key)
{
    const QString k = key == QLatin1String("system") ? (systemIsDark() ? QStringLiteral("midnight") : QStringLiteral("paper")) : key;
    for (const ThemeDef& d : themeTable())
        if (d.key == k) return fromDef(d);
    return midnight();
}

QStringList Theme::groups()
{
    return {QStringLiteral("Reading Themes"), QStringLiteral("Dark Themes"), QStringLiteral("Light Themes")};
}

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
QPushButton#Link { background: transparent; color: @accent; border: 0; padding: 4px 0; text-align: left; }
QPushButton#Link:hover { text-decoration: underline; }
QLineEdit { background: @input; border: 1px solid @line; border-radius: 8px; padding: 8px 10px; selection-background-color: @accent; selection-color: @accentFg; }
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
QMessageBox, QDialog { background: @surface; }
QPushButton { background: @pop; border: 1px solid @line; border-radius: 8px; padding: 6px 14px; }
QPushButton:hover { border-color: @accent; }
QComboBox { background: @input; border: 1px solid @line; border-radius: 8px; padding: 6px 10px; }
QComboBox:hover { border-color: @accent; }
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
        .replace(QLatin1String("@check"), t.accentFg.lightnessF() > 0.5f ? QStringLiteral(":/check-light.svg") : QStringLiteral(":/check-dark.svg"));
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

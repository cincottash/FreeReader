// The themes' colors, without any widget styling (the touch app uses this part too).
#include "Theme.h"

#include <QGuiApplication>
#include <QStyleHints>

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

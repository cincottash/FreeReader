// Themes restyle the whole app: page, header, panels, footer, pop-ups and the library. The table (ThemeData.cpp)
// holds Biblioglot's Reading Themes plus the Dark and Light editor schemes; each becomes a set of color roles.
#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QPalette>
#include <QString>
#include <QStringList>

class QApplication;
class QWidget;

struct ThemeDef {
    QString group;   // "Reading Themes", "Dark Themes", "Light Themes"
    QString name;    // shown in the theme list
    QString key;     // saved in the settings; never renamed
    QHash<QString, QString> colors;
};

const QList<ThemeDef>& themeTable();

struct Theme {
    QString key, name, group;
    QColor bg;        // page background
    QColor surface;   // header, panels, footer
    QColor pop;       // pop-ups
    QColor input;     // text fields
    QColor fg;        // text
    QColor muted;     // secondary text
    QColor line;      // borders and rules
    QColor accent;
    QColor accentHover;
    QColor accentFg;  // text on the accent color
    QColor hl;        // highlight tint
    QColor sel;       // selection tint
    bool dark = true;

    static Theme midnight();
    static Theme fromDef(const ThemeDef& d);
    // A saved choice: a theme key, or "system" (Midnight or Paper, following the operating system's light or
    // dark setting). Unknown keys give Midnight.
    static Theme forChoice(const QString& key);
    static QStringList groups();   // in the order the theme list shows them
    static bool systemIsDark();
};

QPalette themePalette(const Theme& t);
QString themeStyleSheet(const Theme& t);
void applyTheme(QApplication& app, const Theme& t);
// A preview restyles one widget (the window's body) and leaves the theme list itself alone.
void applyThemeTo(QWidget* w, const Theme& t);
void clearThemeFrom(QWidget* w);

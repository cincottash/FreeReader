// Themes: named sets of color roles (see docs). Phase 1 ships Midnight, the mock-up's default; the theme system
// with many built-in and user themes comes in phase 2.
#pragma once

#include <QColor>
#include <QString>

class QApplication;

struct Theme {
    QString key, name;
    QColor bg;        // page background
    QColor surface;   // panels, header, footer
    QColor pop;       // pop-ups
    QColor fg;        // text
    QColor muted;     // secondary text
    QColor line;      // borders and rules
    QColor accent;
    QColor accentFg;  // text on the accent color
    QColor hl;        // highlight tint
    QColor sel;       // selection tint
    bool dark = true;

    static Theme midnight();
};

void applyTheme(QApplication& app, const Theme& t);

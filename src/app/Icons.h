// Line icons from the mock-up, drawn from SVG in the theme's text color.
#pragma once

#include <QColor>
#include <QIcon>

namespace icons {

// back, search, toc, bookmark, panel, commands, fullscreen, exitFullscreen, chevronLeft, chevronRight, close, open
QIcon icon(const char* name, const QColor& color, int size = 20);

}  // namespace icons

// Everything the app remembers between runs, saved as JSON in the app data folder. Books, positions and (later)
// annotations live in the library database instead.
#pragma once

#include "Appearance.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

struct Settings {
    // Window
    QByteArray geometry;
    bool showContents = false;

    // Reading
    Appearance look;                  // All Books; languages and books can override it (library database)
    QString theme = QStringLiteral("midnight");     // a theme key or "system"
    QStringList recentThemes;                        // most recent first; shown as dots in the reader's header
    int readingSpeed = 1100;          // characters per minute, learned from page turns

    // Library
    bool libraryList = false;         // list view instead of covers
    QString librarySort = QStringLiteral("recent");

    // Files
    QString lastFolder;
    QStringList shortcuts;            // "command-id=Ctrl+X" overrides of the default shortcuts

    bool load(const QString& path);
    bool save(const QString& path) const;
};

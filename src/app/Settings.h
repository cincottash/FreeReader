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
    bool showNotebook = false;

    // Reading
    Appearance look;                  // All Books; languages and books can override it (library database)
    QString theme = QStringLiteral("midnight");     // a theme key or "system"
    QStringList recentThemes;                        // most recent first; shown as dots in the reader's header
    int readingSpeed = 1100;          // characters per minute, learned from page turns

    // Library
    bool libraryList = false;         // list view instead of covers
    QString librarySort = QStringLiteral("recent");

    // Translation (API keys are never here: they go to the system's credential store)
    bool autoTranslate = true;        // the pop-up opens when text is selected with the mouse
    bool offlineOnly = false;         // never send text over the internet
    QStringList offlineBooks;         // book ids kept offline
    bool useGemini = true;
    QString geminiModel;              // empty: the default (latest Flash-Lite)
    bool useLmStudio = true;
    QString lmStudioAddress = QStringLiteral("http://localhost:1234");
    bool useDictionary = true;        // the offline dictionary, when one is installed

    // Practice (flashcards)
    QStringList practiceDirections = {QStringLiteral("recognize"), QStringLiteral("recall")};
    bool practiceTyped = false;       // type the answer instead of flipping the card
    bool practiceSentenceFront = false;   // the sentence it came from on the front too
    bool practicePassages = false;
    int practiceNewPerDay = 20;

    // Files
    QString lastFolder;
    QStringList shortcuts;            // "command-id=Ctrl+X" overrides of the default shortcuts

    bool load(const QString& path);
    bool save(const QString& path) const;
};

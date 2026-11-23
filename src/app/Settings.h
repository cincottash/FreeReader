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
    bool showBilingual = false;       // the English of the page in a panel on the right

    // Reading
    Appearance look;                  // All Books; languages and books can override it (library database)
    QString theme = QStringLiteral("midnight");     // a theme key or "system"
    QStringList recentThemes;                        // most recent first; shown as dots in the reader's header
    int readingSpeed = 1100;          // characters per minute, learned from page turns
    bool footerPagesLeft = false;     // the footer shows the pages left in the chapter instead of the time (click it)
    // Page turn animation: none / slide / cover / fade / curl; how long it takes; how it moves (smooth: fast then
    // slowing down, even: the same speed throughout, gentle: slow start and end).
    QString pageTurn = QStringLiteral("none");
    int pageTurnMs = 300;
    QString pageTurnMotion = QStringLiteral("smooth");

    // Library
    bool libraryList = false;         // list view instead of covers
    QString librarySort = QStringLiteral("recent");
    int libraryZoomPct = 100;         // cover size (Ctrl+wheel in the library), 50..250

    // Translation (API keys are never here: they go to the system's credential store)
    // Off: selecting text only selects it (right-click > Translate, or Ctrl+E). A new key replaced "auto_translate"
    // (on by default) on 2026-11-22, so every existing settings file starts with it off, as the owner asked.
    bool translateOnSelect = false;
    bool offlineOnly = false;         // never send text over the internet
    QStringList offlineBooks;         // book ids kept offline
    bool useGemini = true;
    QString geminiModel;              // empty: the default (latest Flash-Lite)
    bool useLmStudio = true;
    QString lmStudioAddress = QStringLiteral("http://localhost:1234");
    QString lmStudioModel;            // empty: whichever model is loaded in LM Studio
    bool useDictionary = true;        // the offline dictionary, when one is installed
    bool useOfflineTranslator = true; // the offline sentence translator (Bergamot), when downloaded

    // Practice (flashcards)
    QStringList practiceDirections = {QStringLiteral("recognize"), QStringLiteral("recall")};
    bool practiceTyped = false;       // type the answer instead of flipping the card
    bool practiceSentenceFront = false;   // the sentence it came from on the front too
    bool practicePassages = false;
    bool practiceHighlights = true;   // highlighted words (up to 4) join the word log, without a lookup
    int practiceNewPerDay = 20;

    // Files
    QString lastFolder;
    QStringList shortcuts;            // "command-id=Ctrl+X" overrides of the default shortcuts

    bool load(const QString& path);
    bool save(const QString& path) const;
};

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
    bool showPractice = false;        // the practice workspace on the right while reading

    // Reading
    Appearance look;                  // All Books; languages and books can override it (library database)
    QString theme = QStringLiteral("midnight");     // a theme key or "system"
    QStringList recentThemes;                        // most recent first; shown as dots in the reader's header
    int readingSpeed = 1100;          // characters per minute, learned from page turns
    // The footer shows the pages left in the chapter (or the time left: click it). Pages by default since
    // 2026-11-23, under a new key so settings files saved before (with the time) start with the pages too.
    bool footerPagesLeft = true;
    QStringList highlightOrder;       // the highlight colors, most recently used first (the dots' order)
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
    // Gemini is the only engine that sends text over the internet: Use Gemini off keeps every book offline. The old
    // "offline_only" switch did the same and was merged into it on 2026-11-23 (a saved one turns Gemini off).
    QStringList offlineBooks;         // book ids kept offline (Gemini isn't asked about them)
    QStringList pageDirections;       // "bookId=rtl" / "bookId=ltr": a book's page direction chosen by hand
    bool useGemini = true;
    QString geminiModel;              // empty: the default (latest Flash-Lite)
    bool useLmStudio = true;
    QString lmStudioAddress = QStringLiteral("http://localhost:1234");
    QString lmStudioModel;            // empty: whichever model is loaded in LM Studio
    bool useDictionary = true;        // the offline dictionary, when one is installed
    bool useOfflineTranslator = true; // the offline sentence translator (Bergamot), when downloaded

    // Audio: Hear Pronunciation and Read Aloud, with the voices installed on this computer.
    int speechRatePct = 0;            // -100 (slowest) .. 100 (fastest); 0: the voice's normal speed
    int speechVolumePct = 100;        // 0..100
    QStringList speechVoices;         // "es=Microsoft Sabina Desktop": a language's voice; none: the system's choice

    // Practice (flashcards)
    QStringList practiceDirections = {QStringLiteral("recognize"), QStringLiteral("recall")};
    bool practiceTyped = false;       // type the answer instead of flipping the card
    bool practiceSentenceFront = false;   // the sentence it came from on the front too
    bool practicePassages = false;
    bool practiceHighlights = true;   // highlighted words (up to 4) join the word log, without a lookup
    int practiceNewPerDay = 20;       // no longer offered (every card is practised); kept so old files still read
    QString practiceOrder = QStringLiteral("random");   // random, alphabetical, newest, oldest, book

    // Files
    QString lastFolder;
    QStringList shortcuts;            // "command-id=Ctrl+X" overrides of the default shortcuts
    QStringList mouseShortcuts;       // "command-id=Back": mouse buttons that run commands
    QStringList libraryFolders;       // folders added with Add Folder: checked for new books at every start

    bool load(const QString& path);
    bool save(const QString& path) const;
};

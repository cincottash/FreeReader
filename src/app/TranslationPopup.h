// The translation pop-up next to the selection: "Translating…" while a lookup runs, then a passage's translation (a
// single word's answer is shown as the Dictionary card instead), an explanation, or why nothing answered. Copy,
// Highlight and Hear Pronunciation, and where the answer came from. It never takes the keyboard focus, so the arrow
// keys keep turning pages.
#pragma once

#include "Theme.h"

#include "core/translate/LanguagePacks.h"
#include "core/translate/Translation.h"

#include <QFrame>

class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;

class TranslationPopup : public QFrame {
    Q_OBJECT
public:
    explicit TranslationPopup(QWidget* parent);
    void setTheme(const Theme& t);

    // `anchor`: the selection's rectangle in the parent's coordinates. `language`: the book's language code.
    void showLoading(const QString& text, const QString& language, const QRect& anchor);
    void showAsking(const QString& provider);
    // With a definition in the book's language, for a book in another language: two sides, one all in the reader's
    // language (the meaning, other meanings, the note), one all in the book's (the definition). `dictionaryFirst`
    // (right-click > Dictionary) starts on the book's side, else the reader's. Flip turns it over; Copy, Flip and
    // Highlight are in the reader's language on both sides.
    void showResult(const core::TranslationResult& r, bool dictionaryFirst = false);
    bool hasTwoSides() const { return m_twoSides; }
    bool showingBookSide() const { return m_twoSides && m_bookSide; }
    void flipSide();
    // The reader's own language (English for now: the translations are English).
    static QString readerLanguage() { return core::languages::readerLanguage(); }   // Settings > General > Your Language
    void showNothing(const QString& why);
    // Explain This Sentence: the sentence in English, then the notes on how it works.
    void showExplanation(const core::TranslationResult& r);
    // Hear Pronunciation can't read this language: why, and (Windows) a button to the system's speech settings.
    void showSpeechProblem(const QString& text, bool canOpenSettings);
    const core::TranslationResult& result() const { return m_result; }
    bool isLoading() const { return m_loading; }
    // The Highlight button beside Copy (the owner's request, 2026-11-23): highlights the looked-up words in the
    // color used last, without the right-click menu; Remove Highlight once they are. Hidden when `available` is off
    // (no book's words: an explanation).
    void setHighlight(bool available, bool highlighted, const QColor& color);

signals:
    void settingsRequested();
    void highlightRequested();
    void speakRequested(const QString& text, const QString& language);
    void speechSettingsRequested();

private:
    void applySide();
    QString copyText() const;
    void setHead(const QString& word, const QString& detail);
    void place();
    QLabel* label(const char* testId, const char* object = nullptr);

    Theme m_theme = Theme::midnight();
    QString m_text, m_language;
    QRect m_anchor;
    core::TranslationResult m_result;
    bool m_loading = false;
    bool m_twoSides = false, m_bookSide = true;
    QPushButton* m_flip;

    QLabel* m_word;
    QLabel* m_detail;
    QToolButton* m_speak;
    QLabel* m_speechNote;
    QPushButton* m_speechSettings;
    QLabel* m_translation;
    QLabel* m_alternatives;
    QLabel* m_note;
    QFrame* m_rule;
    QLabel* m_definitionTitle;
    QLabel* m_definition;
    QPushButton* m_copy;
    QPushButton* m_highlight;
    bool m_canHighlight = false;
    QPushButton* m_settings;
    QLabel* m_source;
    QVBoxLayout* m_lay;
};

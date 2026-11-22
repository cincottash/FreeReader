// The translation pop-up next to the selection (as in the mock-up): the word and its part of speech, the English
// meaning in this sentence, other meanings, a definition in the book's language, the whole sentence in English,
// Copy and Hear Pronunciation, and where the answer came from. It never takes the keyboard focus, so the arrow
// keys keep turning pages.
#pragma once

#include "Theme.h"

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
    void showResult(const core::TranslationResult& r, bool dictionaryFirst = false);
    void showNothing(const QString& why);
    const core::TranslationResult& result() const { return m_result; }
    bool isLoading() const { return m_loading; }

signals:
    void settingsRequested();
    void speakRequested(const QString& text, const QString& language);

private:
    void setHead(const QString& word, const QString& detail);
    void place();
    QLabel* label(const char* testId, const char* object = nullptr);

    Theme m_theme = Theme::midnight();
    QString m_text, m_language;
    QRect m_anchor;
    core::TranslationResult m_result;
    bool m_loading = false;

    QLabel* m_word;
    QLabel* m_detail;
    QToolButton* m_speak;
    QLabel* m_translation;
    QLabel* m_alternatives;
    QLabel* m_note;
    QFrame* m_rule;
    QLabel* m_definitionTitle;
    QLabel* m_definition;
    QLabel* m_sentenceTitle;
    QLabel* m_sentence;
    QPushButton* m_copy;
    QPushButton* m_settings;
    QLabel* m_source;
    QVBoxLayout* m_lay;
};

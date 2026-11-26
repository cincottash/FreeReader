// The "Aa" panel of the reader: theme, font, text size, line spacing, margins, page width, columns, justification,
// hyphenation, and which books a change applies to (All Books, the book's language, This Book); page turn
// animations (for all books).
#pragma once

#include "Appearance.h"

#include <QFrame>
#include <QJsonValue>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

class AppearancePanel : public QFrame {
    Q_OBJECT
public:
    enum Scope { AllBooks = 0, Language = 1, ThisBook = 2 };

    explicit AppearancePanel(QWidget* parent);
    // Shows the values in effect for the open book. languageName empty: no language choice.
    void setValues(const Appearance& a, const QString& themeName, const QString& languageName, bool languageHasOwn, bool bookHasOwn);
    Scope scope() const { return m_scope; }
    void setScope(Scope s);
    void showBelow(const QPoint& topRight);
    // Page turns (for all books, from the settings).
    void setPageTurn(const QString& style, int ms, const QString& motion);
    void setTurnArrows(bool on);   // Arrows at the Sides

signals:
    void valueChanged(const QString& key, const QJsonValue& value);
    void resetRequested(int scope);
    void themesRequested();
    void pageTurnChanged(const QString& style, int ms, const QString& motion);
    void turnArrowsChanged(bool on);

protected:
    void keyPressEvent(QKeyEvent*) override;

private:
    QSlider* slider(int min, int max, int step, const char* key, const char* testId);
    void updateLabels();

    Scope m_scope = AllBooks;
    QPushButton* m_theme;
    QComboBox* m_font;
    QSlider *m_size, *m_spacing, *m_margins, *m_width;
    QLabel *m_sizeValue, *m_spacingValue, *m_marginsValue, *m_widthValue;
    QButtonGroup* m_columns;
    QCheckBox *m_justify, *m_hyphenate;
    QButtonGroup* m_scopes;
    QPushButton* m_languageScope;
    QPushButton* m_resetBook;
    QPushButton* m_resetLanguage;
    QLabel* m_scopeNote;
    QComboBox* m_turnStyle;
    QSlider* m_turnSpeed;
    QLabel* m_turnSpeedValue;
    QComboBox* m_turnMotion;
    class QCheckBox* m_turnArrows = nullptr;
};

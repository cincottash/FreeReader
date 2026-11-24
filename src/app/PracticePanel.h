// The practice workspace beside the page while reading (as Kindle's Annotations pane sits beside the book): a
// compact PracticeView with Flashcards and the Word List (This Page, this book or all books), plus Open Full Window
// and Close in its header.
#pragma once

#include "Theme.h"

#include <QFrame>

class PracticeView;
class QToolButton;

class PracticePanel : public QFrame {
    Q_OBJECT
public:
    explicit PracticePanel(QWidget* parent = nullptr);
    void setView(PracticeView* view);   // made by the main window (it shares the word log and the translator)
    PracticeView* view() const { return m_view; }
    void setTheme(const Theme& t);

signals:
    void closeRequested();
    void fullWindowRequested();

private:
    PracticeView* m_view = nullptr;
    QToolButton* m_full;
    QToolButton* m_close;
};

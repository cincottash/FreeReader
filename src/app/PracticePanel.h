// The practice workspace beside the page while reading (as Kindle's Annotations pane sits beside the book): a
// compact PracticeView with Flashcards and the Word List (This Page, this book or all books), plus Open Full Window
// and Close in its header. Its left edge is dragged to make it wider or narrower.
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

    QWidget* grip() const { return m_grip; }   // its left edge, dragged to change its width

signals:
    void closeRequested();
    void fullWindowRequested();
    void widthDragged(int width);   // the width the drag asks for (the reader keeps it within bounds)

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    QWidget* m_grip = nullptr;
    PracticeView* m_view = nullptr;
    QToolButton* m_full;
    QToolButton* m_close;
};

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
    // Maximize: the panel over the whole page area; its button then says Restore (set by the reader).
    void setMaximized(bool on);
    bool isMaximized() const { return m_maximized; }
    int narrowest() const;   // the least width its contents fit in (360 px at least)

signals:
    void closeRequested();
    void fullWindowRequested();   // its Maximize / Restore button
    void widthDragging(int width);  // during the drag: the width asked for (the panel follows, the page holds its layout)
    void widthDragged(int width);   // on release: the width asked for (the reader keeps it within bounds)

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    QWidget* m_grip = nullptr;
    PracticeView* m_view = nullptr;
    bool m_maximized = false;
    QColor m_fg;
    QToolButton* m_full;
    QToolButton* m_close;
};

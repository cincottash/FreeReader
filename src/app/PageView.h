// The reading surface: one or two columns of laid-out pages, painted from the book engine's layouts. Turns pages
// (keys, wheel, arrows shown on hover), selects text, and reports clicks on links and images.
#pragma once

#include "Appearance.h"
#include "Theme.h"

#include "core/layout/Layout.h"
#include "core/layout/PageRenderer.h"
#include "core/position/Position.h"

#include <QElapsedTimer>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

#include <memory>

namespace core {
class BookEngine;
}

struct LinkClick {
    QString target;
    bool external = false;
    bool noteRef = false;
};

class PageView : public QWidget {
    Q_OBJECT
public:
    explicit PageView(core::BookEngine* engine, QWidget* parent = nullptr);

    void setTheme(const Theme& t);
    void setAppearance(const Appearance& a);
    const Appearance& appearance() const { return m_look; }

    void setPosition(const core::Position& p);
    core::Position position() const { return m_anchor; }   // the first character on screen
    core::Position screenEnd() const;                       // after the last character on screen
    bool isReady() const;                                   // the current chapter is laid out
    int columnCount() const;

    bool canGoNext() const;
    bool canGoPrevious() const;
    bool nextScreen();
    bool previousScreen();

    bool hasSelection() const { return m_selEnd != m_selStart; }
    QString selectedText() const;
    void clearSelection();
    void selectRange(const core::Position& from, const core::Position& to);   // tests
    // Highlights of one chapter, painted under the selection.
    void setHighlights(int spine, const std::vector<layout::Overlay>& overlays);
    const std::vector<layout::Overlay>& highlights() const { return m_highlights; }
    // Word bookmarks of one chapter ([start, end) of each word): a small ribbon over the word.
    void setWordBookmarks(int spine, const std::vector<std::pair<int, int>>& words);
    const std::vector<std::pair<int, int>>& wordBookmarks() const { return m_wordMarks; }
    int offsetAt(const QPointF& pos) const;   // the character under a point, -1 when none
    // Selects the word under a point (widget coordinates); false when there is no text there.
    bool selectWordAt(const QPointF& pos);
    // The word under a point, as chapter offsets [start, end); false when there is no word there.
    bool wordRangeAt(const QPointF& pos, int* start, int* end);
    int selectionSpine() const { return m_selSpine; }
    int selectionStart() const { return qMin(m_selStart, m_selEnd); }
    int selectionEnd() const { return qMax(m_selStart, m_selEnd); }
    // How strongly the selection shows now, 0.2..1 (it blinks); 1 while dragging or when it doesn't blink.
    double selectionStrength() const;
    static void setSelectionBlinkForTests(bool on);
    // The sentence the selection starts in (the whole selection when it spans sentences).
    QString selectionSentence() const;

    // Rectangles of a range on screen (widget coordinates), for pop-up placement.
    QRect rangeRect(int spine, int start, int end) const;

    // Page turn animation (Settings > the Text and Layout panel): style none / slide / cover / fade / curl, how long
    // a turn takes, and how it moves (smooth / even / gentle).
    void setPageTurn(const QString& style, int ms, const QString& motion);
    bool isAnimating() const;
    // A short message over the page for a moment ("Margins: 72 px").
    void showNotice(const QString& text);
    QString notice() const { return m_notice; }
    // The screen as it is drawn without the animation, arrows or notice (also for pictures of the animation).
    QPixmap renderScreen();
    void setTurnProgressForTests(double t);   // freezes the running turn at t (0..1)

signals:
    void positionChanged(const core::Position& p, bool byPageTurn);
    void linkClicked(const LinkClick& link, const QPoint& globalPos);
    void imageClicked(const QString& path);
    void readyChanged();
    void pageTurnedByUser();
    void selectionMade();                              // a word or passage was selected with the mouse
    void selectionCleared();
    // A plain click on the page (no drag): pop-ups beside the text close, with or without a selection.
    void pageClicked();
    void resized();
    void selectionMenuRequested(const QPoint& globalPos);   // right-click on text
    void zoomRequested(int steps);       // Ctrl+wheel over the page: text size
    void marginsRequested(int steps);    // Ctrl+Shift+wheel over the page: margins

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void extendWordSelection(const QPointF& pos);
    void contextMenuEvent(QContextMenuEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    struct Geometry {
        int columns = 1;
        float columnWidth = 600, pageHeight = 800, gap = 64, left = 32, top = 48;
    };
    Geometry geometryFor(QSize size) const;
    void applyParams();
    std::shared_ptr<const layout::ChapterLayout> current() const;
    int firstPage(const layout::ChapterLayout& cl) const;
    QRectF columnRect(int i) const;
    // Which column/page a point is on; -1 when outside the columns.
    int columnAt(const QPointF& pos) const;
    layout::Hit hitAt(const QPointF& pos, int* column = nullptr) const;
    int hoverSide(const QPointF& pos) const;   // -1 left arrow, 1 right arrow, 0 none
    void onLayoutReady(int spine);
    void moveTo(const core::Position& p, bool byTurn);
    bool turnTo(const core::Position& p, int direction);   // a page turn, animated when chosen
    void paintScreen(QPainter& p);
    void paintTurn(QPainter& p);

    core::BookEngine* m_engine;
    Theme m_theme = Theme::midnight();
    Appearance m_look;
    Geometry m_geo;
    QTimer m_resizeTimer;
    core::Position m_anchor;
    std::shared_ptr<const layout::ChapterLayout> m_shown;   // last layout painted (kept while a new one is made)
    QElapsedTimer m_waiting;

    // Selection (one chapter at a time)
    int m_selSpine = -1, m_selStart = 0, m_selEnd = 0, m_selAnchor = 0;
    bool m_pressed = false, m_dragging = false;
    bool m_wordDrag = false;            // the button is held after a double-click: drag selects whole words
    int m_wordStart = 0, m_wordEnd = 0; // the double-clicked word
    QPointF m_pressPos;
    layout::Hit m_pressHit;
    int m_pressSide = 0;
    int m_hover = 0;
    int m_wheel = 0;
    int m_highlightSpine = -1;
    std::vector<layout::Overlay> m_highlights;
    std::vector<std::pair<int, int>> m_wordMarks;
    int m_wordMarksSpine = -1;

    QString m_turnStyle = QStringLiteral("none");
    class QVariantAnimation* m_turn = nullptr;
    QPixmap m_fromPix, m_toPix;
    int m_turnDir = 1;
    int m_zoomWheel = 0;
    QString m_notice;
    QTimer m_noticeTimer;
    // The selection (made with the mouse, not a highlight) blinks gently, so it reads as temporary.
    QTimer m_blinkTimer;
    QElapsedTimer m_blinkClock;
    QRect m_blinkRect;   // where the selection was last painted
    bool selectionBlinks() const;
    void blinkStep();
    float turnZone() const;   // the width of the strip at each side that turns pages
};

// The reading surface: one or two columns of laid-out pages, painted from the book engine's layouts. Turns pages
// (keys, wheel, arrows shown on hover), selects text, and reports clicks on links and images.
#pragma once

#include "Appearance.h"
#include "Theme.h"

#include "core/layout/Layout.h"
#include "core/layout/PageRenderer.h"
#include "core/position/Position.h"

#include <QElapsedTimer>
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
    int offsetAt(const QPointF& pos) const;   // the character under a point, -1 when none
    // Selects the word under a point (widget coordinates); false when there is no text there.
    bool selectWordAt(const QPointF& pos);
    int selectionSpine() const { return m_selSpine; }
    int selectionStart() const { return qMin(m_selStart, m_selEnd); }
    int selectionEnd() const { return qMax(m_selStart, m_selEnd); }
    // The sentence the selection starts in (the whole selection when it spans sentences).
    QString selectionSentence() const;

    // Rectangles of a range on screen (widget coordinates), for pop-up placement.
    QRect rangeRect(int spine, int start, int end) const;

signals:
    void positionChanged(const core::Position& p, bool byPageTurn);
    void linkClicked(const LinkClick& link, const QPoint& globalPos);
    void imageClicked(const QString& path);
    void readyChanged();
    void pageTurnedByUser();
    void selectionMade();                              // a word or passage was selected with the mouse
    void selectionCleared();
    void resized();
    void selectionMenuRequested(const QPoint& globalPos);   // right-click on text

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
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
    QPointF m_pressPos;
    layout::Hit m_pressHit;
    int m_pressSide = 0;
    int m_hover = 0;
    int m_wheel = 0;
    int m_highlightSpine = -1;
    std::vector<layout::Overlay> m_highlights;
};

// The reading surface of the touch app: one page of the open book, drawn by the core's page renderer. Turns pages
// (QML calls nextPage / previousPage from taps and swipes), selects the word under a long press, and reports the
// position so the app can save it.
//
// The page is drawn into an image on the UI thread (the layout's text may only be shaped there) and the image is
// what the scene graph shows.
#pragma once

#include "core/layout/Layout.h"
#include "core/position/Position.h"

#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QQuickPaintedItem>

#include <memory>

#include "MobileApp.h"

class BookPage : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobileApp* app READ app WRITE setApp NOTIFY appChanged)
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectionChanged)
    // True while a finger is still making the selection: it doesn't blink then.
    Q_PROPERTY(bool steady READ isSteady WRITE setSteady NOTIFY steadyChanged)
public:
    explicit BookPage(QQuickItem* parent = nullptr);

    MobileApp* app() const { return m_app; }
    void setApp(MobileApp* app);
    bool isReady() const;

    Q_INVOKABLE bool nextPage();
    Q_INVOKABLE bool previousPage();
    Q_INVOKABLE bool selectWordAt(qreal x, qreal y);   // item coordinates; false when there is no word there
    Q_INVOKABLE void selectSentence();                 // widens the selection to its sentence
    Q_INVOKABLE void clearSelection();
    // Extends the selection to the word under a point (dragging after a long press).
    Q_INVOKABLE void extendSelectionTo(qreal x, qreal y);
    // A tap on a link (follows it) or a picture (shows it full screen); false when there is neither there.
    Q_INVOKABLE bool tapAt(qreal x, qreal y);

    bool hasSelection() const { return m_selEnd > m_selStart; }
    QString selectedText() const;
    QString selectionSentence() const;
    bool isSteady() const { return m_steady; }
    void setSteady(bool on);
    double selectionStrength() const;   // 0.2..1: the selection blinks (it's temporary)
    int selectionSpine() const { return m_selSpine; }
    int selectionStart() const { return m_selStart; }
    int selectionEnd() const { return m_selEnd; }

    void setPosition(const core::Position& p);
    core::Position position() const { return m_anchor; }

    void paint(QPainter* painter) override;

signals:
    void appChanged();
    void readyChanged();
    void selectionChanged();
    void positionChanged();
    void steadyChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void applyParams();
    void onLayoutReady(int spine);
    void moveTo(const core::Position& p);
    void render();
    std::shared_ptr<const layout::ChapterLayout> current() const;
    QRectF pageRect() const;
    int offsetAt(qreal x, qreal y, int* charOffset = nullptr) const;
    bool wordAt(int charOffset, int* start, int* end) const;

    MobileApp* m_app = nullptr;
    core::Position m_anchor;
    std::shared_ptr<const layout::ChapterLayout> m_shown;
    QImage m_image;
    QTimer m_blink;
    QElapsedTimer m_blinkClock;
    bool m_steady = false;
    void selectionMoved();
    float m_marginX = 20, m_top = 14, m_bottom = 30;
    int m_selSpine = -1, m_selStart = 0, m_selEnd = 0, m_selAnchorStart = 0, m_selAnchorEnd = 0;
};

// Small overlays of the reading view: the footnote pop-up and the image viewer.
#pragma once

#include "Theme.h"

#include <QFrame>
#include <QImage>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QScrollArea;

// A footnote shown next to its reference, without leaving the page.
class NotePopup : public QFrame {
    Q_OBJECT
public:
    explicit NotePopup(QWidget* parent);
    // `anchor` is the reference's rectangle in the parent's coordinates.
    void showNote(const QString& text, const QRect& anchor, std::function<void()> goToNote);
    QString text() const;

protected:
    void keyPressEvent(QKeyEvent*) override;

private:
    QLabel* m_text;
    QScrollArea* m_scroll;
    QPushButton* m_go;
    std::function<void()> m_goTo;
};

// An image from the book shown over the window, as large as fits. Click or Esc closes it.
class ImageViewer : public QWidget {
    Q_OBJECT
public:
    explicit ImageViewer(QWidget* parent);
    void setTheme(const Theme& t) { m_theme = t; }
    void showImage(const QImage& img);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    QImage m_image;
    Theme m_theme = Theme::midnight();
};

// A one-line label that shortens its text with "…" to the room it has, instead of making the window wider: a long
// book title in the reader's header can't push the window past the screen. text() stays the whole text; the
// tooltip shows it when it is shortened.
#pragma once

#include <QLabel>
#include <QPainter>
#include <QStyle>

class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setProperty("elides", true);   // layout checks: shortened on purpose
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    QSize minimumSizeHint() const override { return {40, QLabel::minimumSizeHint().height()}; }
    bool isElided() const { return fontMetrics().horizontalAdvance(text()) > contentsRect().width(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const QRect r = contentsRect();
        const QString shown = fontMetrics().elidedText(text(), Qt::ElideRight, r.width());
        style()->drawItemText(&p, r, int(alignment()), palette(), isEnabled(), shown, foregroundRole());
    }
    void resizeEvent(QResizeEvent* e) override
    {
        QLabel::resizeEvent(e);
        setToolTip(isElided() ? text() : QString());
    }
};

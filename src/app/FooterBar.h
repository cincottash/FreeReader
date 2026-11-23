// The progress footer from the mock-up: a thin progress bar with chapter marks, then "Page 31 of 1,072 · 3% ·
// 6 min left in chapter". Clicking the bar jumps to that point of the book.
#pragma once

#include "Theme.h"

#include <QStringList>
#include <QWidget>

#include <vector>

class FooterBar : public QWidget {
    Q_OBJECT
public:
    explicit FooterBar(QWidget* parent = nullptr);
    void setTheme(const Theme& t);
    void setProgress(double fraction, const QStringList& stats);   // stats are shown with gaps between them
    void setMarks(const std::vector<double>& fractions);
    QSize sizeHint() const override;
    QStringList stats() const { return m_stats; }
    // One of the stats can be clicked (the time or the pages left in the chapter); -1: none.
    void setClickableStat(int index, const QString& tooltip);
    QRect statRect(int index) const;   // tests

signals:
    void jumpRequested(double fraction);
    void statClicked(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    Theme m_theme = Theme::midnight();
    double m_fraction = 0;
    double m_hover = -1;
    QStringList m_stats;
    std::vector<double> m_marks;
    int m_clickable = -1;
    QString m_clickableTip;
    int statAt(const QPointF& pos) const;
};

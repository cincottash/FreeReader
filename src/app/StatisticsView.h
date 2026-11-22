// Reading and learning statistics (Go > Statistics): headline numbers as tiles (time read today, this week,
// days in a row, books finished, words, cards learned, reviews, how many were remembered), three bar charts of the
// last 30 days (minutes read, new words, reviews; one measure each, no second scale), the same numbers as a table
// on request, and the books by time read.
#pragma once

#include "Theme.h"

#include "core/services/Statistics.h"

#include <QWidget>

#include <functional>

class QGridLayout;
class QLabel;
class QPushButton;
class QToolButton;
class QTreeWidget;

// A single-series bar chart of one value per day: thin bars with a rounded top, a faint grid, the day and value
// on hover (also its tooltip), keyboard-free and read-only.
class DayBarChart : public QWidget {
    Q_OBJECT
public:
    explicit DayBarChart(QWidget* parent = nullptr);
    void setTheme(const Theme& t);
    // `format` turns a value into its label ("12 min").
    void setData(const QList<QDate>& days, const QList<double>& values, std::function<QString(double)> format);
    QString textAt(int index) const;   // "Mon 3 Nov: 12 min" (tests and the tooltip)
    int barAt(const QPoint& p) const;  // -1 outside the bars

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    QSize sizeHint() const override { return {560, 170}; }

private:
    QRectF plotRect() const;
    double niceMax() const;

    Theme m_theme = Theme::midnight();
    QList<QDate> m_days;
    QList<double> m_values;
    std::function<QString(double)> m_format;
    int m_hover = -1;
};

class StatisticsView : public QWidget {
    Q_OBJECT
public:
    explicit StatisticsView(core::Library* library, QWidget* parent = nullptr);
    void setTheme(const Theme& t);
    void setToday(std::function<QDate()> today) { m_today = std::move(today); }   // tests
    void refresh();
    const core::Statistics& stats() const { return m_stats; }
    QString tileText(const char* id) const;   // a tile's number (tests)

signals:
    void backRequested();
    void openBookRequested(int bookId);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    QLabel* tile(QGridLayout* grid, int row, int col, const char* id, const QString& caption);

    core::Library* m_library;
    std::function<QDate()> m_today;
    core::Statistics m_stats;
    Theme m_theme = Theme::midnight();
    QToolButton* m_back;
    QLabel* m_summary;
    QList<QLabel*> m_tiles;
    DayBarChart* m_reading;
    DayBarChart* m_words;
    DayBarChart* m_reviews;
    QPushButton* m_showTable;
    QTreeWidget* m_table;
    QTreeWidget* m_books;
    QLabel* m_empty;
};

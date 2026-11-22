#include "StatisticsView.h"

#include "Icons.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QString duration(int seconds)
{
    const int minutes = (seconds + 30) / 60;
    if (minutes < 60) return QObject::tr("%1 min").arg(QLocale().toString(minutes));
    return QObject::tr("%1 h %2 min").arg(QLocale().toString(minutes / 60)).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

QString number(int n) { return QLocale().toString(n); }

QLabel* sectionTitle(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("PanelTitle"));
    l->setStyleSheet(QStringLiteral("padding: 16px 0 4px 0;"));
    return l;
}

}  // namespace

// ---------------------------------------------------------------- DayBarChart

DayBarChart::DayBarChart(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void DayBarChart::setTheme(const Theme& t)
{
    m_theme = t;
    update();
}

void DayBarChart::setData(const QList<QDate>& days, const QList<double>& values, std::function<QString(double)> format)
{
    m_days = days;
    m_values = values;
    m_format = std::move(format);
    m_hover = -1;
    // The whole series in words, for screen readers and as the chart's own tooltip.
    double total = 0;
    for (double v : values) total += v;
    setToolTip(tr("Total over %1 days: %2").arg(values.size()).arg(m_format ? m_format(total) : QString::number(total)));
    update();
}

QString DayBarChart::textAt(int i) const
{
    if (i < 0 || i >= m_values.size()) return {};
    return QLocale().toString(m_days.value(i), QStringLiteral("ddd d MMM")) + QStringLiteral(": ") +
           (m_format ? m_format(m_values[i]) : QString::number(m_values[i]));
}

QRectF DayBarChart::plotRect() const { return QRectF(44, 8, width() - 44 - 8, height() - 8 - 24); }

double DayBarChart::niceMax() const
{
    double m = 0;
    for (double v : m_values) m = qMax(m, v);
    // 2, 5 or 10 times a power of ten, at or above the largest value, and at least 2 (so the middle line is a
    // whole number: 0, 1, 2, never 0, 0.5, 1).
    if (m <= 2) return 2;
    const double p = std::pow(10.0, std::floor(std::log10(m)));
    for (double f : {1.0, 2.0, 5.0, 10.0})
        if (f * p >= m && std::fmod(f * p, 2.0) == 0.0) return f * p;
    return 10 * p;
}

int DayBarChart::barAt(const QPoint& pos) const
{
    const QRectF r = plotRect();
    if (m_values.isEmpty() || !r.adjusted(0, -8, 0, 8).contains(pos)) return -1;
    const double slot = r.width() / m_values.size();
    const int i = int((pos.x() - r.left()) / slot);
    return i >= 0 && i < m_values.size() ? i : -1;
}

void DayBarChart::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = plotRect();
    const double max = niceMax();
    QFont small = font();
    small.setPixelSize(11);
    p.setFont(small);
    // A faint grid at 0, half and the top, with its values in muted ink.
    QColor grid = m_theme.line;
    grid.setAlphaF(0.6);
    for (int k = 0; k <= 2; ++k) {
        const double v = max * k / 2.0;
        const double y = r.bottom() - r.height() * k / 2.0;
        p.setPen(QPen(k == 0 ? m_theme.line : grid, 1));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(m_theme.muted);
        const QString label = m_format ? m_format(v) : QString::number(v);
        p.drawText(QRectF(0, y - 8, r.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, label);
    }
    if (m_values.isEmpty()) return;
    // Thin bars with a 2 px gap and a rounded top, anchored to the baseline.
    const double slot = r.width() / m_values.size();
    const double w = qBound(2.0, slot - 2.0, 18.0);
    for (int i = 0; i < m_values.size(); ++i) {
        const double v = m_values[i];
        if (v <= 0) continue;
        const double h = qMax(2.0, r.height() * v / max);
        const QRectF bar(r.left() + i * slot + (slot - w) / 2, r.bottom() - h, w, h);
        const double radius = std::min({4.0, w / 2, h / 2});
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);   // the two shapes overlap: both filled, not cancelled
        path.addRoundedRect(bar, radius, radius);
        path.addRect(QRectF(bar.left(), bar.top() + radius, bar.width(), bar.height() - radius));   // square at the base
        p.fillPath(path.simplified(), i == m_hover ? m_theme.accentHover : m_theme.accent);
    }
    // Days along the bottom: the first, the middle and the last.
    p.setPen(m_theme.muted);
    for (int i : {0, int(m_days.size()) / 2, int(m_days.size()) - 1}) {
        if (i < 0 || i >= m_days.size()) continue;
        const double x = r.left() + (i + 0.5) * slot;
        const QString label = QLocale().toString(m_days[i], QStringLiteral("d MMM"));
        Qt::Alignment a = i == 0 ? Qt::AlignLeft : i == m_days.size() - 1 ? Qt::AlignRight : Qt::AlignHCenter;
        QRectF box(x - 60, r.bottom() + 4, 120, 16);
        if (i == 0) box.moveLeft(qMax(0.0, x - slot / 2));
        if (i == m_days.size() - 1) box.moveRight(qMin(double(width()), x + slot / 2));
        p.drawText(box, a | Qt::AlignTop, label);
    }
}

void DayBarChart::mouseMoveEvent(QMouseEvent* e)
{
    const int i = barAt(e->position().toPoint());
    if (i != m_hover) {
        m_hover = i;
        update();
    }
    if (i >= 0) QToolTip::showText(e->globalPosition().toPoint(), textAt(i), this);
    else QToolTip::hideText();
}

void DayBarChart::leaveEvent(QEvent*)
{
    m_hover = -1;
    update();
}

// ---------------------------------------------------------------- StatisticsView

StatisticsView::StatisticsView(core::Library* library, QWidget* parent) : QWidget(parent), m_library(library)
{
    setObjectName(QStringLiteral("Page"));
    setAttribute(Qt::WA_StyledBackground);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* bar = new QWidget;
    bar->setObjectName(QStringLiteral("Bar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 4, 12, 4);
    h->setSpacing(8);
    m_back = new QToolButton;
    m_back->setObjectName(QStringLiteral("Icon"));
    m_back->setFixedSize(40, 40);
    m_back->setIconSize(QSize(20, 20));
    m_back->setToolTip(tr("Back"));
    m_back->setCursor(Qt::PointingHandCursor);
    m_back->setFocusPolicy(Qt::NoFocus);
    m_back->setProperty("testId", "statisticsBack");
    h->addWidget(m_back);
    auto* heading = new QLabel(tr("Statistics"));
    heading->setObjectName(QStringLiteral("Title"));
    h->addWidget(heading, 1);
    lay->addWidget(bar);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("Page"));
    scroll->setWidget(page);
    lay->addWidget(scroll, 1);
    auto* outer = new QHBoxLayout(page);
    outer->addStretch(1);
    auto* col = new QWidget;
    col->setMaximumWidth(900);
    outer->addWidget(col, 8);
    outer->addStretch(1);
    auto* body = new QVBoxLayout(col);
    body->setContentsMargins(16, 12, 16, 24);
    body->setSpacing(6);

    m_empty = new QLabel(tr("Nothing yet. Reading time is counted as you turn pages; words and reviews come from translating, "
                            "highlighting and practising."));
    m_empty->setObjectName(QStringLiteral("Muted"));
    m_empty->setWordWrap(true);
    m_empty->setProperty("testId", "statisticsEmpty");
    body->addWidget(m_empty);

    body->addWidget(sectionTitle(tr("READING")));
    auto* readGrid = new QGridLayout;
    readGrid->setSpacing(8);
    tile(readGrid, 0, 0, "statToday", tr("Read today"));
    tile(readGrid, 0, 1, "statWeek", tr("Read in the last 7 days"));
    tile(readGrid, 0, 2, "statStreak", tr("Days in a row"));
    tile(readGrid, 0, 3, "statFinished", tr("Books finished"));
    body->addLayout(readGrid);
    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("Muted"));
    m_summary->setWordWrap(true);
    m_summary->setProperty("testId", "statSummary");
    body->addWidget(m_summary);
    body->addWidget(sectionTitle(tr("MINUTES READ, LAST 30 DAYS")));
    m_reading = new DayBarChart;
    m_reading->setProperty("testId", "chartReading");
    body->addWidget(m_reading);

    body->addWidget(sectionTitle(tr("LEARNING")));
    auto* learnGrid = new QGridLayout;
    learnGrid->setSpacing(8);
    tile(learnGrid, 0, 0, "statWords", tr("Words in the word log"));
    tile(learnGrid, 0, 1, "statLearned", tr("Cards learned"));
    tile(learnGrid, 0, 2, "statReviewsToday", tr("Reviews today"));
    tile(learnGrid, 0, 3, "statRetention", tr("Remembered, last 30 days"));
    body->addLayout(learnGrid);
    body->addWidget(sectionTitle(tr("NEW WORDS, LAST 30 DAYS")));
    m_words = new DayBarChart;
    m_words->setProperty("testId", "chartWords");
    body->addWidget(m_words);
    body->addWidget(sectionTitle(tr("REVIEWS, LAST 30 DAYS")));
    m_reviews = new DayBarChart;
    m_reviews->setProperty("testId", "chartReviews");
    body->addWidget(m_reviews);

    m_showTable = new QPushButton(tr("Show the Numbers"));
    m_showTable->setObjectName(QStringLiteral("Link"));
    m_showTable->setCursor(Qt::PointingHandCursor);
    m_showTable->setProperty("testId", "statShowTable");
    body->addWidget(m_showTable, 0, Qt::AlignLeft);
    m_table = new QTreeWidget;
    m_table->setProperty("testId", "statTable");
    m_table->setRootIsDecorated(false);
    m_table->setHeaderLabels({tr("Day"), tr("Minutes Read"), tr("Pages"), tr("New Words"), tr("Reviews")});
    m_table->setMinimumHeight(260);
    m_table->hide();
    body->addWidget(m_table);

    body->addWidget(sectionTitle(tr("BOOKS")));
    m_books = new QTreeWidget;
    m_books->setProperty("testId", "statBooks");
    m_books->setRootIsDecorated(false);
    m_books->setHeaderLabels({tr("Book"), tr("Time Read"), tr("Pages"), tr("Words"), tr("Progress")});
    m_books->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_books->header()->setStretchLastSection(false);
    m_books->setMinimumHeight(200);
    m_books->setToolTip(tr("Double-click a book to open it"));
    body->addWidget(m_books);
    body->addStretch(1);

    connect(m_back, &QToolButton::clicked, this, &StatisticsView::backRequested);
    connect(m_showTable, &QPushButton::clicked, this, [this] {
        m_table->setVisible(m_table->isHidden());
        m_showTable->setText(m_table->isHidden() ? tr("Show the Numbers") : tr("Hide the Numbers"));
    });
    connect(m_books, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) { emit openBookRequested(it->data(0, Qt::UserRole).toInt()); });
}

QLabel* StatisticsView::tile(QGridLayout* grid, int row, int col, const char* id, const QString& caption)
{
    auto* box = new QFrame;
    box->setObjectName(QStringLiteral("Pop"));
    box->setAttribute(Qt::WA_StyledBackground);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(14, 10, 14, 10);
    v->setSpacing(2);
    auto* value = new QLabel(QStringLiteral("0"));
    QFont f = value->font();
    f.setPixelSize(24);
    f.setWeight(QFont::DemiBold);
    value->setFont(f);
    value->setProperty("testId", id);
    auto* text = new QLabel(caption);
    text->setObjectName(QStringLiteral("Muted"));
    text->setWordWrap(true);
    v->addWidget(value);
    v->addWidget(text);
    grid->addWidget(box, row, col);
    grid->setColumnStretch(col, 1);
    m_tiles << value;
    return value;
}

QString StatisticsView::tileText(const char* id) const
{
    for (QLabel* l : m_tiles)
        if (l->property("testId").toString() == QLatin1String(id)) return l->text();
    return {};
}

void StatisticsView::setTheme(const Theme& t)
{
    m_theme = t;
    m_back->setIcon(icons::icon("back", t.fg));
    for (DayBarChart* c : {m_reading, m_words, m_reviews}) c->setTheme(t);
}

void StatisticsView::refresh()
{
    const QDate today = m_today ? m_today() : QDate::currentDate();
    m_stats = core::computeStatistics(m_library, today, 30);
    const core::Statistics& s = m_stats;
    auto set = [this](const char* id, const QString& text) {
        for (QLabel* l : m_tiles)
            if (l->property("testId").toString() == QLatin1String(id)) l->setText(text);
    };
    set("statToday", duration(s.todaySeconds));
    set("statWeek", duration(s.weekSeconds));
    set("statStreak", number(s.streak));
    set("statFinished", number(s.booksFinished));
    set("statWords", number(s.words));
    set("statLearned", number(s.cardsLearned));
    set("statReviewsToday", number(s.reviewsToday));
    set("statRetention", s.retentionMonth < 0 ? QStringLiteral("–") : QStringLiteral("%1%").arg(s.retentionMonth));
    m_summary->setText(tr("Best run: %1 days · Pages turned in the last 7 days: %2 · Books being read: %3 · All time: %4 · New words in the last 7 days: %5")
                           .arg(number(s.bestStreak), number(s.pagesWeek), number(s.booksReading), duration(s.totalSeconds), number(s.wordsWeek)));
    m_empty->setVisible(s.totalSeconds == 0 && s.words == 0 && s.reviewsMonth == 0);

    QList<QDate> days;
    QList<double> minutes, words, reviews;
    for (const core::DayStats& d : s.days) {
        days << d.day;
        minutes << d.readingSeconds / 60.0;
        words << d.newWords;
        reviews << d.reviews;
    }
    auto mins = [](double v) { return QObject::tr("%1 min").arg(QLocale().toString(qRound(v))); };
    auto count = [](double v) { return QLocale().toString(qRound(v)); };
    m_reading->setData(days, minutes, mins);
    m_words->setData(days, words, count);
    m_reviews->setData(days, reviews, count);

    m_table->clear();
    for (int i = int(s.days.size()) - 1; i >= 0; --i) {
        const core::DayStats& d = s.days[i];
        new QTreeWidgetItem(m_table, {QLocale().toString(d.day, QLocale::ShortFormat), number(qRound(d.readingSeconds / 60.0)), number(d.pages),
                                      number(d.newWords), number(d.reviews)});
    }
    m_books->clear();
    for (const core::BookStats& b : s.books) {
        auto* it = new QTreeWidgetItem(m_books, {b.title.isEmpty() ? tr("(a removed book)") : b.title, duration(b.seconds), number(b.pages),
                                                 number(b.words), b.finished ? tr("Finished") : QStringLiteral("%1%").arg(qRound(b.progress * 100))});
        it->setData(0, Qt::UserRole, b.bookId);
        for (int c = 1; c < 5; ++c) it->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    }
}

void StatisticsView::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        emit backRequested();
        return;
    }
    QWidget::keyPressEvent(e);
}

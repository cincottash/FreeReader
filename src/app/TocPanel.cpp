#include "TocPanel.h"

#include "core/format/Book.h"

#include <QApplication>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextLayout>

#include <cmath>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Titles wrap onto more lines instead of being cut off with "…".
class WrapDelegate : public QStyledItemDelegate {
public:
    explicit WrapDelegate(QTreeView* view) : QStyledItemDelegate(view), m_view(view) {}

    int textWidth(const QModelIndex& idx) const
    {
        int depth = 0;
        for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent()) ++depth;
        // The view's width minus room for the scroll bar (always counted, so rows measured before it appears
        // still fit after), the indentation of this level and the 6 px padding on each side.
        return qMax(60, m_view->width() - 2 * m_view->frameWidth() - 12 - m_view->indentation() * (depth + 1) - 12 - 4);
    }
    // Measuring and drawing use this same text layout, so a row is always as tall as its wrapped title.
    static qreal wrap(QTextLayout& tl, qreal width)
    {
        QTextOption o;
        o.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        tl.setTextOption(o);
        qreal y = 0;
        tl.beginLayout();
        for (QTextLine line = tl.createLine(); line.isValid(); line = tl.createLine()) {
            line.setLineWidth(width);
            line.setPosition(QPointF(0, y));
            y += line.height();
        }
        tl.endLayout();
        return y;
    }
    QFont fontFor(const QStyleOptionViewItem& opt, const QModelIndex& idx) const
    {
        QFont f = m_view->font();
        const QVariant vf = idx.data(Qt::FontRole);
        if (vf.isValid()) f = vf.value<QFont>().resolve(f);
        (void)opt;
        return f;
    }
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        QTextLayout tl(idx.data().toString(), fontFor(opt, idx), m_view->viewport());
        const qreal h = wrap(tl, textWidth(idx));
        return {textWidth(idx), int(std::ceil(h)) + 12};
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, idx);
        const QString text = o.text;
        o.text.clear();
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &o, p, o.widget);
        p->save();
        const QVariant fg = idx.data(Qt::ForegroundRole);
        QColor c = fg.isValid() ? fg.value<QBrush>().color() : opt.palette.color(QPalette::Text);
        if (!(opt.state & QStyle::State_Enabled)) c = opt.palette.color(QPalette::Disabled, QPalette::Text);
        p->setPen(c);
        QTextLayout tl(text, fontFor(opt, idx), m_view->viewport());
        wrap(tl, textWidth(idx));
        tl.draw(p, QPointF(opt.rect.left() + 6, opt.rect.top() + 6));
        p->restore();
    }

private:
    QTreeView* m_view;
};

// Re-measures the wrapped rows when the panel's width changes.
class RelayoutOnResize : public QObject {
public:
    explicit RelayoutOnResize(QTreeView* v) : QObject(v), m_view(v) {}
    bool eventFilter(QObject*, QEvent* e) override
    {
        // After the resize has settled: rows are measured with the final width.
        if (e->type() == QEvent::Resize || e->type() == QEvent::Show)
            QTimer::singleShot(0, m_view, [v = m_view] { emit v->model()->layoutChanged(); });
        return false;
    }

private:
    QTreeView* m_view;
};

}  // namespace

TocPanel::TocPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("Panel"));
    setAttribute(Qt::WA_StyledBackground);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto* title = new QLabel(tr("CONTENTS"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    lay->addWidget(title);
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(16);
    m_tree->setUniformRowHeights(false);
    m_tree->setWordWrap(true);
    m_tree->setTextElideMode(Qt::ElideNone);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->header()->setSectionResizeMode(QHeaderView::Stretch);
    m_tree->setContentsMargins(8, 0, 8, 8);
    m_tree->setItemDelegate(new WrapDelegate(m_tree));
    auto* relayout = new RelayoutOnResize(m_tree);
    m_tree->viewport()->installEventFilter(relayout);
    m_tree->installEventFilter(relayout);
    lay->addWidget(m_tree, 1);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        const int idx = it->data(0, Qt::UserRole).toInt();
        if (idx >= 0 && (it->flags() & Qt::ItemIsEnabled)) emit entryActivated(idx);
    });
}

void TocPanel::setTheme(const Theme& t)
{
    m_theme = t;
    setCurrent(m_current);
}

void TocPanel::setEntries(const std::vector<core::TocEntry>& toc, const std::vector<bool>& reachable)
{
    m_tree->clear();
    m_items.clear();
    m_current = -1;
    for (size_t i = 0; i < toc.size(); ++i) {
        const core::TocEntry& e = toc[i];
        QTreeWidgetItem* parent = e.parent >= 0 && size_t(e.parent) < m_items.size() ? m_items[size_t(e.parent)] : nullptr;
        auto* it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        it->setText(0, e.title);
        it->setToolTip(0, e.title);
        it->setData(0, Qt::UserRole, int(i));
        if (i < reachable.size() && !reachable[i]) {
            it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
            it->setToolTip(0, tr("This entry's place in the book couldn't be found."));
        }
        m_items.push_back(it);
    }
    // Small tables of contents open fully; big ones show the top level.
    if (toc.size() <= 40) m_tree->expandAll();
    QTimer::singleShot(0, m_tree, [t = m_tree] { emit t->model()->layoutChanged(); });
}

void TocPanel::setCurrent(int entry)
{
    if (m_current >= 0 && size_t(m_current) < m_items.size()) {
        QTreeWidgetItem* old = m_items[size_t(m_current)];
        QFont f = old->font(0);
        f.setBold(false);
        old->setFont(0, f);
        old->setData(0, Qt::ForegroundRole, QVariant());
    }
    m_current = entry;
    if (entry < 0 || size_t(entry) >= m_items.size()) return;
    QTreeWidgetItem* it = m_items[size_t(entry)];
    QFont f = it->font(0);
    f.setBold(true);
    it->setFont(0, f);
    it->setForeground(0, m_theme.accent);
    for (QTreeWidgetItem* p = it->parent(); p; p = p->parent()) p->setExpanded(true);
    m_tree->scrollToItem(it, QAbstractItemView::EnsureVisible);
}

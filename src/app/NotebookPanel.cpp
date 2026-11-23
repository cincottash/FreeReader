#include "NotebookPanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QIcon swatch(const QColor& c, bool ring = false)
{
    QPixmap pm(28, 28);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (ring) {
        p.setPen(QPen(c, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(3, 2.5, 8, 9), 1.5, 1.5);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QRectF(2, 2, 10, 10));
    }
    return QIcon(pm);
}

QString colorName(const QString& key)
{
    if (key == QLatin1String("pink")) return QObject::tr("Pink");
    if (key == QLatin1String("orange")) return QObject::tr("Orange");
    if (key == QLatin1String("blue")) return QObject::tr("Blue");
    return QObject::tr("Yellow");
}

}  // namespace

NotebookPanel::NotebookPanel(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Panel"));
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral("#Panel { border-right: 0; }"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 8);
    lay->setSpacing(4);
    auto* title = new QLabel(tr("NOTEBOOK"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    lay->addWidget(title);
    auto* top = new QHBoxLayout;
    top->setContentsMargins(16, 0, 16, 4);
    m_filter = new QComboBox;
    m_filter->setProperty("testId", "notebookFilter");
    m_filter->addItem(tr("All"), QStringLiteral("all"));
    m_filter->addItem(tr("Highlights"), QStringLiteral("highlights"));
    m_filter->addItem(tr("Notes"), QStringLiteral("notes"));
    m_filter->addItem(tr("Bookmarks"), QStringLiteral("bookmarks"));
    for (const QString& c : core::Annotations::colors()) m_filter->addItem(swatch(core::Annotations::colorValue(c)), colorName(c), c);
    top->addWidget(m_filter, 1);
    m_export = new QPushButton(tr("Export…"));
    m_export->setToolTip(tr("Saves the highlights, notes and bookmarks as Markdown (or JSON, for other tools)."));
    m_export->setProperty("testId", "notebookExport");
    top->addWidget(m_export);
    lay->addLayout(top);
    m_list = new QListWidget;
    m_list->setProperty("testId", "notebookList");
    m_list->setWordWrap(true);
    m_list->setTextElideMode(Qt::ElideNone);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setSpacing(2);
    lay->addWidget(m_list, 1);
    m_empty = new QLabel(tr("No highlights, notes or bookmarks yet. Right-click text to highlight it or add a note; the "
                            "bookmark button marks the page."));
    m_empty->setObjectName(QStringLiteral("Muted"));
    m_empty->setWordWrap(true);
    m_empty->setContentsMargins(20, 8, 20, 8);
    m_empty->setProperty("testId", "notebookEmpty");
    lay->addWidget(m_empty);
    lay->addStretch(0);

    connect(m_filter, &QComboBox::currentIndexChanged, this, &NotebookPanel::fill);
    connect(m_export, &QPushButton::clicked, this, &NotebookPanel::exportRequested);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) { emit activated(it->data(Qt::UserRole).toInt()); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { emit activated(it->data(Qt::UserRole).toInt()); });
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QListWidgetItem* it = m_list->itemAt(at);
        if (!it) return;
        const int id = it->data(Qt::UserRole).toInt();
        const bool highlight = it->data(Qt::UserRole + 1).toBool();
        QMenu menu(this);
        menu.addAction(tr("Go There"), this, [this, id] { emit activated(id); });
        if (highlight) {
            menu.addAction(tr("Edit Note…"), this, [this, id] { emit editNoteRequested(id); });
            QMenu* colors = menu.addMenu(tr("Color"));
            for (const QString& c : core::Annotations::colors())
                colors->addAction(swatch(core::Annotations::colorValue(c)), colorName(c), this, [this, id, c] { emit colorRequested(id, c); });
        }
        menu.addSeparator();
        menu.addAction(tr("Delete"), this, [this, id] { emit deleteRequested(id); });
        menu.exec(m_list->viewport()->mapToGlobal(at));
    });
    fill();
}

void NotebookPanel::setTheme(const Theme& t)
{
    m_theme = t;
    fill();
}

QString NotebookPanel::filter() const { return m_filter->currentData().toString(); }

void NotebookPanel::setFilter(const QString& key)
{
    const int at = m_filter->findData(key);
    if (at >= 0) m_filter->setCurrentIndex(at);
}

void NotebookPanel::setAnnotations(const QList<core::Annotation>& list)
{
    m_all = list;
    fill();
}

void NotebookPanel::fill()
{
    const QString f = filter();
    const int keepRow = m_list->currentRow();
    m_list->clear();
    for (const core::Annotation& a : m_all) {
        const bool hl = a.kind == core::Annotation::Highlight;
        if (f == QLatin1String("highlights") && !hl) continue;
        if (f == QLatin1String("notes") && a.note.trimmed().isEmpty()) continue;
        if (f == QLatin1String("bookmarks") && hl) continue;
        if (core::Annotations::colors().contains(f) && (!hl || a.color != f)) continue;
        QString text;
        if (hl) {
            QString quote = a.text.simplified();
            if (quote.size() > 220) quote = quote.left(220) + QStringLiteral("…");
            text = QStringLiteral("“%1”").arg(quote);
            if (!a.note.trimmed().isEmpty()) text += QStringLiteral("\n✎ ") + a.note.trimmed();
        } else {
            text = (core::Annotations::isWordBookmark(a) ? tr("Word Bookmark") : tr("Bookmark")) + QStringLiteral(" · ") + a.text.simplified().left(80);
        }
        text += QLatin1Char('\n') + tr("Page %1").arg(a.pageLabel) + (a.chapter.isEmpty() ? QString() : QStringLiteral(" · ") + a.chapter);
        auto* it = new QListWidgetItem(hl ? swatch(core::Annotations::colorValue(a.color)) : swatch(m_theme.accent, true), text, m_list);
        it->setData(Qt::UserRole, a.id);
        it->setData(Qt::UserRole + 1, hl);
        it->setToolTip(hl ? tr("Click to go there. Right-click to edit the note, change the color or delete.") : tr("Click to go there."));
    }
    if (keepRow >= 0 && keepRow < m_list->count()) m_list->setCurrentRow(keepRow);
    m_empty->setVisible(m_all.isEmpty());
    m_list->setVisible(!m_all.isEmpty());
    m_export->setEnabled(!m_all.isEmpty());
}

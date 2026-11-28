#include "LibraryView.h"

#include "Icons.h"
#include "Settings.h"

#include "core/format/FormatPlugin.h"
#include "core/services/Importer.h"
#include "core/translate/LanguagePacks.h"

#include <QAction>
#include <QApplication>
#include <QCollator>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QWheelEvent>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace {

const char* kBookMime = "application/x-freereader-book-ids";

enum Role {
    RoleId = Qt::UserRole + 1,
    RoleAuthor,
    RoleProgress,
    RoleFinished,
    RoleStarted,
    RoleMissing,
    RoleLang,
    RoleSearch,
    RoleRecent,
    RoleAdded,
    RoleHasCover,
    RoleCollections,
};

// Lowercase without accents, so "quijote" finds "Quijote" and "cancion" finds "canción".
QString folded(const QString& s)
{
    const QString d = s.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(d.size());
    for (QChar c : d)
        if (c.category() != QChar::Mark_NonSpacing) out += c.toLower();
    return out;
}

QString languageName(const QString& code)
{
    const QString c = code.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
    if (c.isEmpty()) return QObject::tr("Unknown Language");
    const QLocale loc(c);
    return loc.language() == QLocale::C ? c : core::languages::displayTitle(c);   // in the app's language
}

// "1 book", "1,180 books": the system's number format, a real singular.
QString count(int n, const QString& one, const QString& many) { return (n == 1 ? one : many).arg(QLocale().toString(n)); }

QString primaryLanguage(const QString& code) { return code.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower(); }

// Books dragged from the grid or list carry their ids.
class BookItemModel : public QStandardItemModel {
public:
    using QStandardItemModel::QStandardItemModel;
    QStringList mimeTypes() const override { return {QString::fromLatin1(kBookMime)}; }
    QMimeData* mimeData(const QModelIndexList& indexes) const override
    {
        QStringList ids;
        for (const QModelIndex& i : indexes)
            if (i.column() == 0) ids << QString::number(i.data(RoleId).toInt());
        auto* md = new QMimeData;
        md->setData(QString::fromLatin1(kBookMime), ids.join(QLatin1Char(',')).toLatin1());
        return md;
    }
    Qt::ItemFlags flags(const QModelIndex& i) const override { return QStandardItemModel::flags(i) | Qt::ItemIsDragEnabled; }
};

// The side panel; collections accept books dropped on them.
class FilterList : public QListWidget {
public:
    std::function<void(int collectionId, const QList<int>& books)> onDrop;
    explicit FilterList(QWidget* parent = nullptr) : QListWidget(parent)
    {
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        viewport()->setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DropOnly);
    }

protected:
    int collectionAt(const QPoint& p) const
    {
        QListWidgetItem* it = itemAt(p);
        const QString key = it ? it->data(Qt::UserRole).toString() : QString();
        return key.startsWith(QLatin1String("collection:")) ? key.mid(11).toInt() : 0;
    }
    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasFormat(QString::fromLatin1(kBookMime))) e->acceptProposedAction();
        else e->ignore();
    }
    void dragMoveEvent(QDragMoveEvent* e) override
    {
        if (e->mimeData()->hasFormat(QString::fromLatin1(kBookMime)) && collectionAt(e->position().toPoint())) {
            setCurrentItem(itemAt(e->position().toPoint()));
            e->acceptProposedAction();
        } else {
            e->ignore();
        }
    }
    void dropEvent(QDropEvent* e) override
    {
        const int c = collectionAt(e->position().toPoint());
        if (!c || !onDrop) return;
        QList<int> ids;
        for (const QByteArray& b : e->mimeData()->data(QString::fromLatin1(kBookMime)).split(','))
            if (b.toInt()) ids << b.toInt();
        onDrop(c, ids);
        e->acceptProposedAction();
    }
};

// Counts at the right of the side panel's entries; group names as small titles.
class FilterDelegate : public QStyledItemDelegate {
public:
    Theme* theme;
    FilterDelegate(Theme* t, QObject* p) : QStyledItemDelegate(p), theme(t) {}
    QSize sizeHint(const QStyleOptionViewItem& o, const QModelIndex& i) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(o, i);
        // A heading or a filter.
        s.setHeight(i.data(Qt::UserRole + 2).toBool() ? 34 : 32);
        return s;
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& i) const override
    {
        const bool header = i.data(Qt::UserRole + 2).toBool();
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, i);
        const QString text = o.text, count = i.data(Qt::UserRole + 1).toString();
        o.text.clear();
        if (!header) QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &o, p, o.widget);
        p->save();
        QFont f = opt.font;
        if (header) {
            f.setPixelSize(11);
            f.setWeight(QFont::DemiBold);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
            p->setPen(theme->muted);
        } else {
            const bool link = i.data(Qt::UserRole).toString() == QLatin1String("new-collection");
            p->setPen(link ? theme->accent : theme->fg);
            if (i.data(Qt::UserRole + 3).toBool()) f.setWeight(QFont::DemiBold);   // the current filter
        }
        p->setFont(f);
        const QRect r = opt.rect.adjusted(header ? 12 : 14, header ? 8 : 0, -12, 0);
        // A long name (a collection's) is shortened with "…" before it reaches the count.
        const QFontMetrics nameFm(f), countFm(opt.font);
        const int room = r.width() - (count.isEmpty() ? 0 : countFm.horizontalAdvance(count) + 10);
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, nameFm.elidedText(text, Qt::ElideRight, qMax(20, room)));
        if (!count.isEmpty()) {
            p->setPen(theme->muted);
            p->drawText(r, Qt::AlignVCenter | Qt::AlignRight, count);
        }
        if (i.data(Qt::UserRole + 3).toBool()) p->fillRect(QRect(opt.rect.left() + 2, opt.rect.top() + 7, 3, opt.rect.height() - 14), theme->accent);
        p->restore();
    }
};

}  // namespace

// Filtering and sorting of the books.
class BookFilter : public QSortFilterProxyModel {
public:
    QString filter = QStringLiteral("all");
    QString search;   // folded
    QString sortKey = QStringLiteral("recent");
    const QHash<int, QSet<int>>* members = nullptr;
    QCollator collator;

    BookFilter(QObject* parent) : QSortFilterProxyModel(parent)
    {
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
    }
    void refilter() { invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override
    {
        const QModelIndex i = sourceModel()->index(row, 0, parent);
        if (!search.isEmpty() && !i.data(RoleSearch).toString().contains(search)) return false;
        const bool finished = i.data(RoleFinished).toBool(), started = i.data(RoleStarted).toBool();
        if (filter == QLatin1String("reading")) return started && !finished;
        if (filter == QLatin1String("finished")) return finished;
        if (filter == QLatin1String("notstarted")) return !started && !finished;
        if (filter.startsWith(QLatin1String("lang:"))) return i.data(RoleLang).toString() == filter.mid(5);
        if (filter.startsWith(QLatin1String("collection:")))
            return members && members->value(filter.mid(11).toInt()).contains(i.data(RoleId).toInt());
        return true;
    }
    bool lessThan(const QModelIndex& a0, const QModelIndex& b0) const override
    {
        const QModelIndex a = a0.siblingAtColumn(0), b = b0.siblingAtColumn(0);
        auto text = [&](int role) { return collator.compare(a.data(role).toString(), b.data(role).toString()); };
        const int byTitle = collator.compare(a.data(Qt::DisplayRole).toString(), b.data(Qt::DisplayRole).toString());
        int c = 0;
        if (sortKey == QLatin1String("title")) c = byTitle;
        else if (sortKey == QLatin1String("author")) c = text(RoleAuthor);
        else if (sortKey == QLatin1String("language")) c = text(RoleLang);
        else if (sortKey == QLatin1String("progress")) c = a.data(RoleProgress).toDouble() > b.data(RoleProgress).toDouble() ? -1 : a.data(RoleProgress).toDouble() < b.data(RoleProgress).toDouble() ? 1 : 0;
        else if (sortKey == QLatin1String("added")) c = a.data(RoleAdded).toLongLong() > b.data(RoleAdded).toLongLong() ? -1 : a.data(RoleAdded).toLongLong() < b.data(RoleAdded).toLongLong() ? 1 : 0;
        else c = a.data(RoleRecent).toLongLong() > b.data(RoleRecent).toLongLong() ? -1 : a.data(RoleRecent).toLongLong() < b.data(RoleRecent).toLongLong() ? 1 : 0;
        return c != 0 ? c < 0 : byTitle < 0;
    }
};

namespace {

// A cover tile: the cover (or one drawn from the title), the title, the author, and progress.
class CoverDelegate : public QStyledItemDelegate {
public:
    Theme* theme;
    core::Library* library;
    mutable QHash<int, QImage> covers;   // loaded on first paint
    CoverDelegate(Theme* t, core::Library* l, QObject* p) : QStyledItemDelegate(p), theme(t), library(l) {}

    double scale = 1.0;   // the library zoom (Ctrl+wheel)
    int coverW() const { return qRound(144 * scale); }
    int coverH() const { return qRound(216 * scale); }
    int tileW() const { return coverW() + 32; }
    int tileH() const { return coverH() + 92; }
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {tileW(), tileH()}; }

    QImage cover(int id) const
    {
        auto it = covers.find(id);
        if (it != covers.end()) return *it;
        QImage img;
        img.loadFromData(library->cover(id));
        covers.insert(id, img);
        return img;
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& i) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        const QRect r = opt.rect;
        if (opt.state & (QStyle::State_MouseOver | QStyle::State_Selected)) {
            QColor bg = (opt.state & QStyle::State_Selected) ? theme->hl : theme->fg;
            if (!(opt.state & QStyle::State_Selected)) bg.setAlphaF(0.06f);
            QPainterPath path;
            path.addRoundedRect(QRectF(r).adjusted(4, 2, -4, -2), 10, 10);
            p->fillPath(path, bg);
        }
        const bool missing = i.data(RoleMissing).toBool();
        const QString title = i.data(Qt::DisplayRole).toString(), author = i.data(RoleAuthor).toString();
        const int kW = tileW();
        QRectF coverRect(r.left() + (kW - coverW()) / 2.0, r.top() + 10, coverW(), coverH());
        const QImage img = i.data(RoleHasCover).toBool() ? cover(i.data(RoleId).toInt()) : QImage();
        if (missing) p->setOpacity(0.35);
        if (!img.isNull()) {
            QSizeF s = img.size();
            s.scale(coverRect.size(), Qt::KeepAspectRatio);
            const QRectF ir(coverRect.left() + (coverRect.width() - s.width()) / 2, coverRect.bottom() - s.height(), s.width(), s.height());
            QPainterPath clip;
            clip.addRoundedRect(ir, 4, 4);
            p->setClipPath(clip);
            p->drawImage(ir, img);
            p->setClipping(false);
            p->setPen(QPen(theme->line, 1));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(ir.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        } else {
            // A cover drawn from the title, in a color that stays the same for each book.
            const int hue = int(qHash(title) % 360);
            QColor c = QColor::fromHsl(hue, 70, theme->dark ? 70 : 170);
            QPainterPath path;
            path.addRoundedRect(coverRect, 4, 4);
            p->fillPath(path, c);
            p->fillRect(QRectF(coverRect.left() + 10, coverRect.top(), 2, coverRect.height()), c.lighter(130));
            p->setPen(theme->dark ? QColor(0xF2, 0xEE, 0xE6) : QColor(0x1E, 0x1B, 0x16));
            QFont f(QStringLiteral("Literata"));
            f.setPixelSize(qMax(9, qRound(17 * scale)));
            f.setWeight(QFont::DemiBold);
            p->setFont(f);
            p->drawText(coverRect.adjusted(20, 22, -12, -50), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, title);
            f.setPixelSize(qMax(8, qRound(12 * scale)));
            f.setWeight(QFont::Normal);
            p->setFont(f);
            p->drawText(coverRect.adjusted(20, 0, -12, -16), Qt::AlignBottom | Qt::AlignLeft | Qt::TextWordWrap, author);
        }
        p->setOpacity(1);

        // Title (up to two lines; the full title is in the tooltip), author, progress.
        QFont tf = opt.font;
        tf.setWeight(QFont::DemiBold);
        p->setFont(tf);
        p->setPen(theme->fg);
        const QFontMetrics tm(tf);
        const QRect textR(r.left() + 12, int(coverRect.bottom()) + 8, kW - 24, tm.height() * 2);
        QString shown = title;
        if (tm.boundingRect(textR, Qt::TextWordWrap, title).height() > textR.height()) {
            int cut = title.size();
            while (cut > 0 && tm.boundingRect(textR, Qt::TextWordWrap, title.left(cut) + QStringLiteral("…")).height() > textR.height()) --cut;
            shown = title.left(cut).trimmed() + QStringLiteral("…");
        }
        p->drawText(textR, Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, shown);
        const int used = tm.boundingRect(textR, Qt::TextWordWrap, shown).height();
        QFont af = opt.font;
        af.setPixelSize(12);
        p->setFont(af);
        p->setPen(theme->muted);
        const QFontMetrics am(af);
        int y = textR.top() + used + 2;
        p->drawText(QRect(textR.left(), y, textR.width(), am.height()), Qt::AlignLeft, am.elidedText(author, Qt::ElideRight, textR.width()));
        y += am.height() + 6;
        const double prog = i.data(RoleProgress).toDouble();
        if (missing) {
            p->setPen(theme->accent);
            p->drawText(QRect(textR.left(), y - 3, textR.width(), am.height()), Qt::AlignLeft, QObject::tr("File Not Found"));
        } else if (i.data(RoleFinished).toBool()) {
            p->setPen(theme->accent);
            p->drawText(QRect(textR.left(), y - 3, textR.width(), am.height()), Qt::AlignLeft, QObject::tr("✓ Finished"));
        } else if (!i.data(RoleStarted).toBool()) {
            p->setPen(theme->accent);
            p->drawText(QRect(textR.left(), y - 3, textR.width(), am.height()), Qt::AlignLeft, QObject::tr("New"));
        } else {
            const int barW = textR.width() - 44;
            p->fillRect(QRect(textR.left(), y + 3, barW, 3), theme->line);
            p->fillRect(QRectF(textR.left(), y + 3, barW * prog, 3), theme->accent);
            p->setPen(theme->muted);
            p->drawText(QRect(textR.left() + barW, y - 3, 44, am.height()), Qt::AlignRight,
                        QLocale().toString(int(std::floor(prog * 100 + 1e-6))) + QStringLiteral("%"));
        }
        p->restore();
    }
};

}  // namespace

LibraryView::LibraryView(core::Library* library, Settings& settings, QWidget* parent)
    : QWidget(parent), m_library(library), m_settings(settings)
{
    setObjectName(QStringLiteral("Page"));
    setAttribute(Qt::WA_StyledBackground);
    setAcceptDrops(true);
    m_importer = new core::Importer(this);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // Header
    auto* bar = new QWidget;
    bar->setObjectName(QStringLiteral("Bar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(16, 8, 10, 8);
    h->setSpacing(10);
    auto* name = new QLabel(QStringLiteral(APP_DISPLAY_NAME));
    name->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600;"));
    h->addWidget(name);
    m_notKept = new QPushButton(tr("Settings Not Kept"));
    m_notKept->setObjectName(QStringLiteral("Link"));
    m_notKept->setCursor(Qt::PointingHandCursor);
    m_notKept->setToolTip(tr("App data is kept in a temporary folder and deleted when the app closes. Click to choose a folder."));
    m_notKept->hide();
    h->addWidget(m_notKept);
    h->addStretch(1);
    // Practice is the big button under Library on the left (rebuildFilters).
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search Title or Author"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(300);
    m_search->setProperty("testId", "search");
    h->addWidget(m_search);
    // Beyond titles and authors: the text of every book (with what is typed in the box, if anything).
    m_searchInside = new QPushButton(tr("Search Inside Books"));
    m_searchInside->setObjectName(QStringLiteral("Chip"));
    m_searchInside->setCursor(Qt::PointingHandCursor);
    m_searchInside->setToolTip(tr("A word or phrase in the text of every book (Ctrl+Shift+F)"));
    m_searchInside->setProperty("testId", "librarySearchInside");
    h->addWidget(m_searchInside);
    connect(m_searchInside, &QPushButton::clicked, this, [this] { emit librarySearchRequested(m_search->text()); });
    // The bookmark added last, in any book (the owner's request, 2026-11-25): the reader's icon, no words (with
    // words, the Spanish header no longer fit a 900 px window).
    m_latestBookmark = new QToolButton;
    m_latestBookmark->setObjectName(QStringLiteral("Icon"));
    m_latestBookmark->setFixedSize(36, 36);
    m_latestBookmark->setIconSize(QSize(20, 20));
    m_latestBookmark->setCursor(Qt::PointingHandCursor);
    m_latestBookmark->setToolTip(tr("Go to Latest Bookmark (Ctrl+Shift+D)"));
    m_latestBookmark->setProperty("testId", "libraryLatestBookmark");
    h->addWidget(m_latestBookmark);
    connect(m_latestBookmark, &QToolButton::clicked, this, &LibraryView::latestBookmarkRequested);
    m_add = new QPushButton(tr("Add Books"));
    m_add->setObjectName(QStringLiteral("Primary"));
    m_add->setCursor(Qt::PointingHandCursor);
    m_add->setProperty("testId", "addBooks");
    auto* addMenu = new QMenu(m_add);
    addMenu->addAction(tr("Add Books…"), this, &LibraryView::chooseFilesToAdd);
    addMenu->addAction(tr("Add Folder…"), this, &LibraryView::chooseFolderToAdd);
    m_add->setMenu(addMenu);
    h->addWidget(m_add);
    lay->addWidget(bar);

    // Body: filters + books
    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    // The left column: the filters, and Practice at its bottom, below Collections (the owner's request, 2026-11-24).
    auto* side = new QWidget;
    side->setObjectName(QStringLiteral("Panel"));
    side->setAttribute(Qt::WA_StyledBackground);
    side->setFixedWidth(240);
    auto* sideLay = new QVBoxLayout(side);
    sideLay->setContentsMargins(0, 0, 0, 0);
    sideLay->setSpacing(0);
    auto* filters = new FilterList;
    m_filters = filters;
    m_filters->setObjectName(QStringLiteral("Filters"));
    m_filters->setItemDelegate(new FilterDelegate(&m_theme, m_filters));
    m_filters->setContextMenuPolicy(Qt::CustomContextMenu);
    m_filters->setProperty("testId", "filters");
    m_filters->setFrameShape(QFrame::NoFrame);
    m_filters->setStyleSheet(QStringLiteral("QListWidget#Filters { background: transparent; border: 0; padding-top: 8px; } "
                                            "QListWidget::item { padding: 0; }"));
    filters->onDrop = [this](int c, const QList<int>& ids) { addToCollection(c, ids); };
    sideLay->addWidget(m_filters, 1);
    {
        auto* practiceTitle = new QLabel(tr("PRACTICE"));
        practiceTitle->setObjectName(QStringLiteral("PanelTitle"));
        practiceTitle->setStyleSheet(QStringLiteral("padding: 10px 12px 6px 12px;"));
        sideLay->addWidget(practiceTitle);
        auto* box = new QVBoxLayout;
        box->setContentsMargins(12, 0, 12, 14);
        box->setSpacing(6);
        m_practice = new QPushButton(tr("Practice Words"));
        m_practice->setObjectName(QStringLiteral("BigPractice"));
        m_practice->setCursor(Qt::PointingHandCursor);
        m_practice->setToolTip(tr("Flashcards of the words you looked up or highlighted (Ctrl+P). While reading, the "
                                  "Practice button opens them beside the page."));
        m_practice->setProperty("testId", "libraryPractice");
        m_practice->setFixedHeight(36);
        m_practice->setIconSize(QSize(18, 18));
        box->addWidget(m_practice);
        m_practiceCountLabel = new QLabel;
        m_practiceCountLabel->setObjectName(QStringLiteral("Muted"));
        m_practiceCountLabel->setStyleSheet(QStringLiteral("font-size: 12px; padding-left: 2px;"));
        m_practiceCountLabel->setProperty("testId", "libraryPracticeCount");
        box->addWidget(m_practiceCountLabel);
        sideLay->addLayout(box);
        connect(m_practice, &QPushButton::clicked, this, &LibraryView::practiceRequested);
    }
    body->addWidget(side);

    auto* main = new QVBoxLayout;
    main->setContentsMargins(24, 16, 24, 0);
    main->setSpacing(10);
    auto* top = new QHBoxLayout;
    m_heading = new QLabel;
    m_heading->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 600;"));
    m_heading->setProperty("testId", "heading");
    top->addWidget(m_heading);
    top->addStretch(1);
    auto* sortLabel = new QLabel(tr("Sort"));
    sortLabel->setObjectName(QStringLiteral("Muted"));
    top->addWidget(sortLabel);
    m_sort = new QComboBox;
    m_sort->setProperty("testId", "sort");
    const struct { const char* key; QString text; } sorts[] = {{"recent", tr("Recently Read")}, {"title", tr("Title")}, {"author", tr("Author")},
                                                               {"language", tr("Language")}, {"progress", tr("Progress")}, {"added", tr("Date Added")}};
    for (const auto& s : sorts) m_sort->addItem(s.text, QString::fromLatin1(s.key));
    top->addWidget(m_sort);
    m_gridButton = new QToolButton;
    m_listButton = new QToolButton;
    for (QToolButton* b : {m_gridButton, m_listButton}) {
        b->setObjectName(QStringLiteral("Icon"));
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFixedSize(36, 36);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        top->addWidget(b);
    }
    m_gridButton->setToolTip(tr("Covers"));
    m_listButton->setToolTip(tr("List"));
    m_gridButton->setProperty("testId", "gridView");
    m_listButton->setProperty("testId", "listView");
    main->addLayout(top);

    m_message = new QLabel;
    m_message->setWordWrap(true);
    m_message->setProperty("testId", "message");
    m_message->setTextFormat(Qt::PlainText);
    m_message->hide();
    main->addWidget(m_message);

    m_model = new BookItemModel(this);
    m_model->setHorizontalHeaderLabels({tr("Title"), tr("Author"), tr("Language"), tr("Progress"), tr("Last Read"), tr("Added")});
    m_proxy = new BookFilter(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->members = &m_members;

    m_views = new QStackedWidget;
    m_grid = new QListView;
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setUniformItemSizes(true);
    m_grid->setSpacing(6);
    m_grid->setMouseTracking(true);
    m_grid->setModel(m_proxy);
    m_coverDelegate = new CoverDelegate(&m_theme, m_library, m_grid);
    m_grid->setItemDelegate(m_coverDelegate);
    m_grid->viewport()->installEventFilter(this);
    static_cast<CoverDelegate*>(m_coverDelegate)->scale = m_settings.libraryZoomPct / 100.0;
    m_grid->setDragEnabled(true);
    m_grid->setDragDropMode(QAbstractItemView::DragOnly);
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    m_grid->setCursor(Qt::PointingHandCursor);
    m_grid->setProperty("testId", "grid");
    m_list = new QTreeView;
    m_list->setModel(m_proxy);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAlternatingRowColors(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setSortingEnabled(false);
    m_list->header()->setStretchLastSection(false);
    // The title takes the room; the author a fixed share; language, progress and the dates as wide as they need,
    // so a small window still shows titles (before 2026-11-24 the fixed columns left a title "Una …" at 1024 px).
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_list->header()->resizeSection(1, 170);
    for (int c = 2; c < 6; ++c) m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_list->header()->setMinimumSectionSize(60);
    m_list->setStyleSheet(QStringLiteral("QHeaderView::section { padding: 6px 6px 6px 10px; border: 0; } QTreeView::item { padding: 6px 4px; }"));
    m_list->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_list->setProperty("testId", "list");
    m_views->addWidget(m_grid);
    m_views->addWidget(m_list);
    main->addWidget(m_views, 1);
    m_empty = new QLabel;
    m_empty->setObjectName(QStringLiteral("Muted"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setProperty("testId", "empty");
    main->addWidget(m_empty, 1);
    body->addLayout(main, 1);
    lay->addLayout(body, 1);

    // Continue Reading
    m_continueBar = new QWidget;
    m_continueBar->setObjectName(QStringLiteral("Footer"));
    m_continueBar->setAttribute(Qt::WA_StyledBackground);
    auto* cb = new QHBoxLayout(m_continueBar);
    cb->setContentsMargins(20, 8, 16, 8);
    m_continueText = new QLabel;
    m_continueText->setTextFormat(Qt::RichText);
    m_continueText->setProperty("testId", "continueText");
    m_continueText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // a long title never widens the window
    cb->addWidget(m_continueText, 1);
    m_continue = new QPushButton(tr("Continue"));
    m_continue->setObjectName(QStringLiteral("Primary"));
    m_continue->setCursor(Qt::PointingHandCursor);
    m_continue->setProperty("testId", "continue");
    cb->addWidget(m_continue);
    lay->addWidget(m_continueBar);

    connect(m_notKept, &QPushButton::clicked, this, &LibraryView::settingsLinkClicked);
    connect(m_search, &QLineEdit::textChanged, this, &LibraryView::setSearch);
    connect(m_sort, &QComboBox::activated, this, [this](int i) { setSort(m_sort->itemData(i).toString()); });
    connect(m_gridButton, &QToolButton::clicked, this, [this] { setListMode(false); });
    connect(m_listButton, &QToolButton::clicked, this, [this] { setListMode(true); });
    connect(m_grid, &QListView::clicked, this, &LibraryView::activate);
    connect(m_list, &QTreeView::activated, this, &LibraryView::activate);
    connect(m_list, &QTreeView::doubleClicked, this, &LibraryView::activate);
    connect(m_grid, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) { showBookMenu(m_grid->viewport()->mapToGlobal(p), m_grid->indexAt(p)); });
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) { showBookMenu(m_list->viewport()->mapToGlobal(p), m_list->indexAt(p)); });
    connect(m_filters, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        const QString key = it->data(Qt::UserRole).toString();
        if (key == QLatin1String("new-collection")) newCollection();
        else if (!key.isEmpty()) setFilter(key);
    });
    connect(m_filters, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& p) { showCollectionMenu(m_filters->viewport()->mapToGlobal(p), m_filters->itemAt(p)); });
    connect(m_continue, &QPushButton::clicked, this, [this] {
        if (m_continueId) openBookById(m_continueId);
    });
    connect(m_importer, &core::Importer::progress, this, [this](int done, int total, const QString& name) {
        if (!name.isEmpty()) setMessage(tr("Adding books… %1 of %2: %3").arg(QLocale().toString(done + 1), QLocale().toString(total), name));
    });
    auto failures = std::make_shared<QStringList>();
    auto duplicates = std::make_shared<QStringList>();
    connect(m_importer, &core::Importer::bookRead, this, [this, duplicates](const core::ImportedBook& b) {
        if (!m_library) return;
        // A book already in the library (the same file elsewhere, or the same book in another file) isn't added
        // again; when its old file is gone, the library uses this one instead.
        if (const int dup = m_library->duplicateOf(b.path, b.meta, b.contentHash)) {
            const core::LibraryBook old = m_library->book(dup);
            if (!QFileInfo::exists(old.path)) {
                m_library->setPath(dup, b.path);
                m_library->addBook(b.path, b.meta, b.coverPng, b.contentHash);
            } else {
                *duplicates << (old.title.isEmpty() ? QFileInfo(b.path).fileName() : old.title);
                m_library->rememberDuplicate(b.path, dup);
            }
            return;
        }
        m_library->addBook(b.path, b.meta, b.coverPng, b.contentHash);
    });
    connect(m_importer, &core::Importer::failed, this, [failures](const QString& path, const QString& message) {
        *failures << QStringLiteral("• %1: %2").arg(QFileInfo(path).fileName(), message);
    });
    connect(m_importer, &core::Importer::finished, this, [this, failures, duplicates](int read, int bad) {
        const int dups = int(duplicates->size());
        const int added = read - dups;
        const bool quiet = m_quietImport;
        m_quietImport = false;
        // The folder check at start says nothing when nothing changed.
        if (quiet && read == 0 && bad == 0) {
            refresh();
            return;
        }
        QString msg = read == 0 && bad == 0 ? tr("No EPUB books were found there.")
                      : quiet               ? count(added, tr("Added %1 new book from your folders."), tr("Added %1 new books from your folders."))
                                            : count(added, tr("Added %1 book."), tr("Added %1 books."));
        if (dups)
            msg += QLatin1Char('\n') + count(dups, tr("%1 was already in the library, so it wasn't added again:"),
                                             tr("%1 were already in the library, so they weren't added again:")) +
                   QLatin1Char(' ') + duplicates->join(QStringLiteral(", "));
        if (bad) msg += QLatin1Char('\n') + count(bad, tr("%1 file couldn't be added:"), tr("%1 files couldn't be added:")) + QLatin1Char('\n') + failures->join(QLatin1Char('\n'));
        failures->clear();
        duplicates->clear();
        setMessage(msg);
        refresh();
    });

    m_sort->setCurrentIndex(qMax(0, m_sort->findData(m_settings.librarySort)));
    m_proxy->sortKey = m_sort->currentData().toString();
    setListMode(m_settings.libraryList);
    setTheme(m_theme);
    refresh();
}

LibraryView::~LibraryView() = default;

void LibraryView::setTheme(const Theme& t)
{
    m_theme = t;
    m_gridButton->setIcon(icons::icon("grid", t.fg));
    m_latestBookmark->setIcon(icons::icon("bookmarkGo", t.fg));
    m_listButton->setIcon(icons::icon("list", t.fg));
    if (auto* d = dynamic_cast<CoverDelegate*>(m_grid->itemDelegate())) d->covers.clear();
    m_grid->viewport()->update();
    m_filters->viewport()->update();
    m_practice->setIcon(icons::icon("cards", t.accentFg, 18));
}

void LibraryView::updatePracticeCount()
{
    const int words = m_practiceCount ? m_practiceCount() : 0;
    m_practiceCountLabel->setText(words == 0   ? tr("No Words Yet")
                                  : words == 1 ? tr("1 Word in the Word List")
                                               : tr("%1 Words in the Word List").arg(QLocale().toString(words)));
}

void LibraryView::setSettingsNotKept(bool on) { m_notKept->setVisible(on); }

void LibraryView::setMessage(const QString& text)
{
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
}

QString LibraryView::message() const { return m_message->isVisible() ? m_message->text() : QString(); }

bool LibraryView::isImporting() const { return m_importer->isBusy(); }

void LibraryView::refresh()
{
    m_books = m_library ? m_library->books() : QList<core::LibraryBook>();
    m_members = m_library ? m_library->collectionMembers() : QHash<int, QSet<int>>();
    if (auto* d = dynamic_cast<CoverDelegate*>(m_grid->itemDelegate())) d->covers.clear();
    m_model->removeRows(0, m_model->rowCount());
    const QLocale loc;
    for (const core::LibraryBook& b : m_books) {
        const QString title = b.title.isEmpty() ? QFileInfo(b.path).completeBaseName() : b.title;
        auto* it = new QStandardItem(title);
        it->setEditable(false);
        it->setData(b.id, RoleId);
        it->setData(b.author, RoleAuthor);
        it->setData(b.progress, RoleProgress);
        it->setData(b.finished(), RoleFinished);
        it->setData(b.progress > 0.0005, RoleStarted);
        it->setData(!QFileInfo::exists(b.path), RoleMissing);
        it->setData(primaryLanguage(b.language), RoleLang);
        it->setData(folded(title + QLatin1Char(' ') + b.author), RoleSearch);
        it->setData(b.lastOpened.isValid() ? b.lastOpened.toMSecsSinceEpoch() : -1 - (QDateTime::currentMSecsSinceEpoch() - b.added.toMSecsSinceEpoch()), RoleRecent);
        it->setData(b.added.toMSecsSinceEpoch(), RoleAdded);
        it->setData(b.hasCover, RoleHasCover);
        it->setToolTip(title + (b.author.isEmpty() ? QString() : QStringLiteral("\n") + b.author) + QStringLiteral("\n") + QDir::toNativeSeparators(b.path));
        QList<QStandardItem*> row{it};
        row << new QStandardItem(b.author) << new QStandardItem(languageName(b.language))
            << new QStandardItem(b.finished() ? tr("Finished") : b.progress > 0.0005 ? loc.toString(int(std::floor(b.progress * 100 + 1e-6))) + QStringLiteral("%") : tr("New"))
            << new QStandardItem(b.lastOpened.isValid() ? loc.toString(b.lastOpened.toLocalTime().date(), QLocale::ShortFormat) : QString())
            << new QStandardItem(loc.toString(b.added.toLocalTime().date(), QLocale::ShortFormat));
        for (QStandardItem* s : row) s->setEditable(false);
        m_model->appendRow(row);
    }
    m_proxy->refilter();
    m_proxy->sort(0);
    rebuildFilters();
    updateHeading();
    updateContinue();
}

void LibraryView::rebuildFilters()
{
    m_filters->clear();
    auto header = [&](const QString& text) {
        auto* it = new QListWidgetItem(text, m_filters);
        it->setFlags(Qt::ItemIsEnabled);
        it->setData(Qt::UserRole + 2, true);
    };
    auto entry = [&](const QString& text, const QString& key, int count) {
        auto* it = new QListWidgetItem(text, m_filters);
        it->setData(Qt::UserRole, key);
        it->setData(Qt::UserRole + 1, count >= 0 ? QLocale().toString(count) : QString());
        it->setData(Qt::UserRole + 3, key == m_filter);
        return it;
    };
    int reading = 0, finished = 0, notStarted = 0;
    QMap<QString, int> langs;
    for (const core::LibraryBook& b : m_books) {
        if (b.finished()) ++finished;
        else if (b.progress > 0.0005) ++reading;
        else ++notStarted;
        langs[primaryLanguage(b.language)]++;
    }
    header(tr("LIBRARY"));
    entry(tr("All Books"), QStringLiteral("all"), int(m_books.size()));
    entry(tr("Reading"), QStringLiteral("reading"), reading);
    entry(tr("Finished"), QStringLiteral("finished"), finished);
    entry(tr("Not Started"), QStringLiteral("notstarted"), notStarted);
    updatePracticeCount();
    if (!langs.isEmpty()) {
        header(tr("LANGUAGES"));
        QList<QPair<QString, QString>> sorted;
        for (auto it = langs.cbegin(); it != langs.cend(); ++it) sorted << qMakePair(languageName(it.key()), it.key());
        std::sort(sorted.begin(), sorted.end());
        for (const auto& l : sorted) entry(l.first, QStringLiteral("lang:") + l.second, langs.value(l.second));
    }
    header(tr("COLLECTIONS"));
    if (m_library)
        for (const core::Collection& c : m_library->collections()) {
            QListWidgetItem* it = entry(c.name, QStringLiteral("collection:%1").arg(c.id), c.count);
            it->setToolTip(c.name + QLatin1Char('\n') + tr("Drag books here to add them. Right-click to rename or delete."));
        }
    QListWidgetItem* add = entry(tr("+ New Collection"), QStringLiteral("new-collection"), -1);
    add->setToolTip(tr("Make a collection, then drag books onto it."));
    // A filter that no longer exists (a deleted collection) falls back to All Books.
    bool found = false;
    for (int i = 0; i < m_filters->count(); ++i)
        if (m_filters->item(i)->data(Qt::UserRole).toString() == m_filter) found = true;
    if (!found) setFilter(QStringLiteral("all"));
}

void LibraryView::updateHeading()
{
    QString name = tr("All Books");
    for (int i = 0; i < m_filters->count(); ++i)
        if (m_filters->item(i)->data(Qt::UserRole).toString() == m_filter) name = m_filters->item(i)->text();
    const int n = m_proxy->rowCount();
    m_heading->setText(name + QStringLiteral(" · ") + count(n, tr("%1 book"), tr("%1 books")));
    const bool emptyLibrary = m_books.isEmpty();
    m_empty->setText(emptyLibrary ? tr("No books yet.\nClick Add Books, or drop EPUB files or a folder anywhere in this window.\nThe files stay where they are.")
                                  : tr("No books match."));
    m_empty->setVisible(n == 0);
    m_views->setVisible(n > 0);
}

void LibraryView::updateContinue()
{
    m_continueId = 0;
    const core::LibraryBook* best = nullptr;
    for (const core::LibraryBook& b : m_books)
        if (b.lastOpened.isValid() && !b.finished() && QFileInfo::exists(b.path) && (!best || b.lastOpened > best->lastOpened)) best = &b;
    m_continueBar->setVisible(best != nullptr);
    if (!best) return;
    m_continueId = best->id;
    const QString title = best->title.size() > 60 ? best->title.left(58).trimmed() + QStringLiteral("…") : best->title;
    QStringList parts{tr("Continue Reading: <b>%1</b>").arg(title.toHtmlEscaped())};
    m_continueText->setToolTip(best->title);
    if (!best->pageLabel.isEmpty() && !best->pageCount.isEmpty()) parts << tr("Page %1 of %2").arg(best->pageLabel, best->pageCount);
    parts << QLocale().toString(int(std::floor(best->progress * 100 + 1e-6))) + QStringLiteral("%");
    m_continueText->setText(parts.join(QStringLiteral(" · ")));
}

void LibraryView::setFilter(const QString& key)
{
    m_filter = key;
    m_proxy->filter = key;
    m_proxy->refilter();
    for (int i = 0; i < m_filters->count(); ++i)
        m_filters->item(i)->setData(Qt::UserRole + 3, m_filters->item(i)->data(Qt::UserRole).toString() == key);
    updateHeading();
}

void LibraryView::setSearch(const QString& text)
{
    if (m_search->text() != text) m_search->setText(text);
    m_proxy->search = folded(text.trimmed());
    m_proxy->refilter();
    updateHeading();
}

void LibraryView::setSort(const QString& key)
{
    const int i = m_sort->findData(key);
    if (i < 0) return;
    m_sort->setCurrentIndex(i);
    m_proxy->sortKey = key;
    m_proxy->invalidate();
    m_proxy->sort(0);
    m_settings.librarySort = key;
}

void LibraryView::setListMode(bool list)
{
    m_views->setCurrentWidget(list ? static_cast<QWidget*>(m_list) : m_grid);
    m_listButton->setChecked(list);
    m_gridButton->setChecked(!list);
    m_settings.libraryList = list;
}

QList<int> LibraryView::visibleBookIds() const
{
    QList<int> out;
    for (int r = 0; r < m_proxy->rowCount(); ++r) out << m_proxy->index(r, 0).data(RoleId).toInt();
    return out;
}

const core::LibraryBook* LibraryView::bookById(int id) const
{
    for (const core::LibraryBook& b : m_books)
        if (b.id == id) return &b;
    return nullptr;
}

int LibraryView::bookIdAt(const QModelIndex& index) const { return index.isValid() ? index.siblingAtColumn(0).data(RoleId).toInt() : 0; }

void LibraryView::activate(const QModelIndex& index)
{
    if (QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) return;   // selecting, not opening
    if (const int id = bookIdAt(index)) openBookById(id);
}

void LibraryView::openBookById(int id)
{
    const core::LibraryBook* b = bookById(id);
    if (!b) return;
    if (!QFileInfo::exists(b->path)) {
        setMessage(tr("The file of \"%1\" isn't at %2 any more. Right-click the book to locate it or remove it from the library.")
                       .arg(b->title, QDir::toNativeSeparators(b->path)));
        return;
    }
    emit openRequested(b->path);
}

void LibraryView::locateBook(int id, const QString& newPath)
{
    if (!m_library || newPath.isEmpty()) return;
    const int other = m_library->bookIdForPath(newPath);
    if (other && other != id) {
        setMessage(tr("That file is already in the library."));
        return;
    }
    m_library->setPath(id, newPath);
    setMessage(QString());
    refresh();
}

void LibraryView::removeBook(int id)
{
    if (!m_library) return;
    m_library->removeBook(id);
    refresh();
}

void LibraryView::addToCollection(int collectionId, const QList<int>& bookIds)
{
    if (!m_library) return;
    for (int id : bookIds) m_library->addToCollection(collectionId, id);
    refresh();
}

void LibraryView::setFinished(int id, bool finished)
{
    if (!m_library) return;
    m_library->setFinished(id, finished);
    refresh();
}

void LibraryView::addBooks(const QStringList& filesOrFolders)
{
    if (filesOrFolders.isEmpty()) return;
    setMessage(tr("Adding books…"));
    m_importer->add(filesOrFolders);
}

void LibraryView::chooseFilesToAdd()
{
    QStringList patterns;
    for (const QString& s : core::FormatRegistry::instance().fileSuffixes()) patterns << QStringLiteral("*.") + s;
    const QString dir = m_settings.lastFolder.isEmpty() ? QDir::homePath() : m_settings.lastFolder;
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Books"), dir, tr("E-books (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (files.isEmpty()) return;
    m_settings.lastFolder = QFileInfo(files.first()).absolutePath();
    addBooks(files);
}

void LibraryView::chooseFolderToAdd()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add a Folder of Books"), m_settings.lastFolder.isEmpty() ? QDir::homePath() : m_settings.lastFolder);
    if (dir.isEmpty()) return;
    m_settings.lastFolder = dir;
    addFolder(dir);
}

void LibraryView::addFolder(const QString& dir)
{
    const QString d = QDir::cleanPath(QDir::fromNativeSeparators(dir));
    if (!m_settings.libraryFolders.contains(d, Qt::CaseInsensitive)) {
        m_settings.libraryFolders << d;
        emit settingsChanged();
    }
    addBooks({d});
}

void LibraryView::checkFolders()
{
    if (!m_library || m_settings.libraryFolders.isEmpty() || isImporting()) return;
    QStringList existing;
    for (const QString& f : m_settings.libraryFolders)
        if (QFileInfo(f).isDir()) existing << f;   // a folder that is gone (an unplugged drive) is skipped this time
    if (existing.isEmpty()) return;
    m_quietImport = true;
    m_importer->add(existing, m_library->bookPaths());
}

void LibraryView::newCollection()
{
    if (!m_prompt) return;
    m_prompt(tr("New Collection"), tr("Collection name"), [this](const QString& name) -> QString {
        if (name.trimmed().isEmpty()) return tr("Type a name for the collection.");
        const int id = m_library ? m_library->createCollection(name) : 0;
        if (!id) return tr("There is already a collection called \"%1\".").arg(name.trimmed());
        refresh();
        setFilter(QStringLiteral("collection:%1").arg(id));
        setMessage(tr("Drag books onto \"%1\" in the side panel to add them.").arg(name.trimmed()));
        return {};
    });
}

void LibraryView::showBookMenu(const QPoint& globalPos, const QModelIndex& index)
{
    const int id = bookIdAt(index);
    const core::LibraryBook* b = bookById(id);
    if (!b) return;
    QAbstractItemView* view = m_views->currentWidget() == m_list ? static_cast<QAbstractItemView*>(m_list) : m_grid;
    if (!view->selectionModel()->isSelected(index)) view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QList<int> ids;
    for (const QModelIndex& i : view->selectionModel()->selectedIndexes())
        if (i.column() == 0) ids << i.data(RoleId).toInt();
    if (ids.isEmpty()) ids << id;

    QMenu menu(this);
    const bool missing = !QFileInfo::exists(b->path);
    if (!missing) menu.addAction(tr("Open"), this, [this, id] { openBookById(id); });
    if (missing)
        menu.addAction(tr("Locate File…"), this, [this, id] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Locate Book File"), m_settings.lastFolder, tr("E-books (*.epub)"));
            locateBook(id, f);
        });
    else
        menu.addAction(tr("Show File in Folder"), this, [b] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(b->path).absolutePath())); });
    QMenu* add = menu.addMenu(tr("Add to Collection"));
    if (m_library)
        for (const core::Collection& c : m_library->collections()) add->addAction(c.name, this, [this, c, ids] { addToCollection(c.id, ids); });
    if (!add->isEmpty()) add->addSeparator();
    add->addAction(tr("New Collection…"), this, [this, ids] {
        if (!m_prompt) return;
        m_prompt(tr("New Collection"), tr("Collection name"), [this, ids](const QString& name) -> QString {
            const int c = m_library ? m_library->createCollection(name) : 0;
            if (!c) return name.trimmed().isEmpty() ? tr("Type a name for the collection.") : tr("There is already a collection called \"%1\".").arg(name.trimmed());
            addToCollection(c, ids);
            return {};
        });
    });
    if (m_filter.startsWith(QLatin1String("collection:"))) {
        const int c = m_filter.mid(11).toInt();
        menu.addAction(tr("Remove from This Collection"), this, [this, c, ids] {
            for (int i : ids) m_library->removeFromCollection(c, i);
            refresh();
        });
    }
    menu.addSeparator();
    if (b->finished()) menu.addAction(tr("Mark as Not Finished"), this, [this, ids] { for (int i : ids) setFinished(i, false); });
    else menu.addAction(tr("Mark as Finished"), this, [this, ids] { for (int i : ids) setFinished(i, true); });
    menu.addAction(tr("Remove from Library"), this, [this, ids] {
        const QString what = ids.size() == 1 ? tr("\"%1\"").arg(bookById(ids.first()) ? bookById(ids.first())->title : QString())
                                             : count(int(ids.size()), tr("%1 book"), tr("%1 books"));
        if (m_confirm && !m_confirm(tr("Remove from Library"), tr("Remove %1 from the library? The files stay where they are.").arg(what))) return;
        for (int i : ids) m_library->removeBook(i);
        refresh();
    });
    menu.exec(globalPos);
}

void LibraryView::showCollectionMenu(const QPoint& globalPos, QListWidgetItem* item)
{
    const QString key = item ? item->data(Qt::UserRole).toString() : QString();
    if (!key.startsWith(QLatin1String("collection:")) || !m_library) return;
    const int id = key.mid(11).toInt();
    const QString name = item->text();
    QMenu menu(this);
    menu.addAction(tr("Rename…"), this, [this, id] {
        if (!m_prompt) return;
        m_prompt(tr("Rename Collection"), tr("New name"), [this, id](const QString& n) -> QString {
            if (!m_library->renameCollection(id, n)) return n.trimmed().isEmpty() ? tr("Type a name.") : tr("That name is taken.");
            refresh();
            return {};
        });
    });
    menu.addAction(tr("Delete Collection"), this, [this, id, name] {
        if (m_confirm && !m_confirm(tr("Delete Collection"), tr("Delete the collection \"%1\"? Its books stay in the library.").arg(name))) return;
        m_library->deleteCollection(id);
        refresh();
    });
    menu.exec(globalPos);
}

void LibraryView::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void LibraryView::dropEvent(QDropEvent* e)
{
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    addBooks(paths);
    e->acceptProposedAction();
}

int LibraryView::zoom() const { return m_settings.libraryZoomPct; }

void LibraryView::zoomTo(int pct)
{
    pct = qBound(50, pct, 250);
    if (pct == m_settings.libraryZoomPct) return;
    m_settings.libraryZoomPct = pct;
    static_cast<CoverDelegate*>(m_coverDelegate)->scale = pct / 100.0;
    // The tiles have a new size: lay the grid out again so as many as fit go on each row.
    emit m_coverDelegate->sizeHintChanged(m_proxy->index(0, 0));
    m_grid->setUniformItemSizes(false);
    m_grid->setUniformItemSizes(true);
    m_grid->doItemsLayout();
    m_grid->viewport()->update();
    emit settingsChanged();
}

bool LibraryView::eventFilter(QObject* watched, QEvent* e)
{
    // Ctrl+wheel over the covers: bigger or smaller covers (10 % a step).
    if (watched == m_grid->viewport() && e->type() == QEvent::Wheel) {
        auto* w = static_cast<QWheelEvent*>(e);
        if (w->modifiers() & Qt::ControlModifier) {
            m_zoomWheel += w->angleDelta().y() ? w->angleDelta().y() : w->angleDelta().x();
            while (qAbs(m_zoomWheel) >= 120) {
                const int step = m_zoomWheel > 0 ? 1 : -1;
                m_zoomWheel -= step * 120;
                zoomTo(m_settings.libraryZoomPct + step * 10);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, e);
}

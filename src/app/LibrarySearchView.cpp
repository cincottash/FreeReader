#include "LibrarySearchView.h"

#include "Icons.h"

#include "core/services/Library.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

enum Role { BookIdRole = Qt::UserRole, PathRole, SpineRole, StartRole, EndRole };

QString count(int n, const QString& one, const QString& many) { return QStringLiteral("%1 %2").arg(QLocale().toString(n), n == 1 ? one : many); }

}  // namespace

LibrarySearchView::LibrarySearchView(core::Library* library, QWidget* parent) : QWidget(parent), m_library(library)
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
    m_back->setProperty("testId", "librarySearchBack");
    h->addWidget(m_back);
    auto* heading = new QLabel(tr("Search All Books"));
    heading->setObjectName(QStringLiteral("Title"));
    h->addWidget(heading, 1);
    lay->addWidget(bar);

    auto* body = new QVBoxLayout;
    body->setContentsMargins(16, 12, 16, 12);
    body->setSpacing(8);
    auto* row = new QHBoxLayout;
    m_edit = new QLineEdit;
    m_edit->setPlaceholderText(tr("A word or phrase to find in every book"));
    m_edit->setClearButtonEnabled(true);
    m_edit->setProperty("testId", "librarySearchBox");
    m_go = new QPushButton(tr("Search"));
    m_go->setObjectName(QStringLiteral("Primary"));
    m_go->setProperty("testId", "librarySearchGo");
    row->addWidget(m_edit, 1);
    row->addWidget(m_go);
    body->addLayout(row);
    auto* opts = new QHBoxLayout;
    m_whole = new QCheckBox(tr("Whole Words"));
    m_whole->setProperty("testId", "librarySearchWhole");
    m_case = new QCheckBox(tr("Match Case"));
    m_case->setProperty("testId", "librarySearchCase");
    m_accents = new QCheckBox(tr("Match Accents"));
    m_accents->setToolTip(tr("Off: “razon” also finds “razón”."));
    m_accents->setProperty("testId", "librarySearchAccents");
    opts->addWidget(m_whole);
    opts->addWidget(m_case);
    opts->addWidget(m_accents);
    opts->addStretch(1);
    body->addLayout(opts);
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("Muted"));
    m_status->setWordWrap(true);
    m_status->setProperty("testId", "librarySearchStatus");
    body->addWidget(m_status);
    m_results = new QTreeWidget;
    m_results->setProperty("testId", "librarySearchResults");
    m_results->setHeaderHidden(true);
    m_results->setColumnCount(1);
    m_results->setWordWrap(true);
    m_results->setUniformRowHeights(false);
    m_results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_results->setTextElideMode(Qt::ElideNone);
    body->addWidget(m_results, 1);
    lay->addLayout(body, 1);

    connect(m_back, &QToolButton::clicked, this, [this] {
        cancel();
        emit backRequested();
    });
    connect(m_go, &QPushButton::clicked, this, [this] {
        if (isSearching()) cancel();
        else search();
    });
    connect(m_edit, &QLineEdit::returnPressed, this, &LibrarySearchView::search);
    connect(m_results, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) { activate(it); });
    connect(m_results, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        if (it->parent()) activate(it);   // a result; a book's row only opens or closes its list
    });
    connect(&m_search, &core::LibrarySearch::bookSearched, this, [this](int id, const core::LibraryBookHits& r, int done, int total) {
        if (id == m_searchId) addBook(r, done, total);
    });
    connect(&m_search, &core::LibrarySearch::finished, this, [this](int id, int hits, int books, bool canceled) {
        if (id != m_searchId) return;
        m_go->setText(tr("Search"));
        QString text;
        if (canceled) text = tr("Stopped. %1 in %2 so far.").arg(count(hits, tr("result"), tr("results")), count(books, tr("book"), tr("books")));
        else if (hits == 0) text = tr("No results for “%1” in any book.").arg(m_query);
        else text = tr("%1 in %2.").arg(count(hits, tr("result"), tr("results")), count(books, tr("book"), tr("books")));
        if (m_unreadable > 0) text += QLatin1Char(' ') + tr("%1 couldn't be read.").arg(count(m_unreadable, tr("book"), tr("books")));
        m_status->setText(text);
    });
}

void LibrarySearchView::setTheme(const Theme& t) { m_back->setIcon(icons::icon("back", t.fg)); }

QString LibrarySearchView::status() const { return m_status->text(); }

bool LibrarySearchView::isSearching() const { return m_search.isRunning(); }

void LibrarySearchView::setQuery(const QString& query)
{
    m_edit->setText(query.simplified());
    search();
}

void LibrarySearchView::cancel()
{
    if (m_search.isRunning()) m_search.cancel();
}

void LibrarySearchView::search()
{
    const QString q = m_edit->text().simplified();
    m_results->clear();
    m_unreadable = 0;
    if (q.size() < 2) {
        cancel();
        m_status->setText(q.isEmpty() ? QString() : tr("Type at least 2 letters."));
        return;
    }
    QList<core::LibrarySearch::BookRef> books;
    if (m_library)
        for (const core::LibraryBook& b : m_library->books()) books << core::LibrarySearch::BookRef{b.id, b.path, b.title, b.author};
    if (books.isEmpty()) {
        m_status->setText(tr("The library has no books yet."));
        return;
    }
    core::SearchOptions o;
    o.wholeWord = m_whole->isChecked();
    o.matchCase = m_case->isChecked();
    o.matchAccents = m_accents->isChecked();
    m_query = q;
    m_status->setText(tr("Searching %1…").arg(count(int(books.size()), tr("book"), tr("books"))));
    m_go->setText(tr("Stop"));
    m_searchId = m_search.start(books, q, o);
}

void LibrarySearchView::addBook(const core::LibraryBookHits& r, int done, int total)
{
    if (!r.error.isEmpty()) ++m_unreadable;
    if (m_search.isRunning())
        m_status->setText(tr("Searching… %1 of %2 books.").arg(QLocale().toString(done), QLocale().toString(total)));
    if (r.hits.isEmpty()) return;
    const QString title = r.title.isEmpty() ? r.path : r.title;
    QString head = title + (r.author.isEmpty() ? QString() : QStringLiteral(" · ") + r.author) + QStringLiteral("  (") +
                   (r.truncated ? tr("first %1").arg(QLocale().toString(r.hits.size())) : QLocale().toString(r.hits.size())) + QLatin1Char(')');
    auto* book = new QTreeWidgetItem(m_results, {head});
    QFont f = book->font(0);
    f.setWeight(QFont::DemiBold);
    book->setFont(0, f);
    book->setData(0, BookIdRole, r.bookId);
    book->setData(0, PathRole, r.path);
    for (const core::SearchHit& hit : r.hits) {
        auto* it = new QTreeWidgetItem(book, {(QStringLiteral("…") + hit.before + QStringLiteral("‹") + hit.match + QStringLiteral("›") + hit.after +
                                               QStringLiteral("…")).simplified()});
        it->setData(0, BookIdRole, r.bookId);
        it->setData(0, PathRole, r.path);
        it->setData(0, SpineRole, hit.spine);
        it->setData(0, StartRole, hit.start);
        it->setData(0, EndRole, hit.end);
        it->setToolTip(0, tr("Open the book here"));
    }
    // The first few books open, the rest folded (long lists stay readable).
    book->setExpanded(m_results->topLevelItemCount() <= 3);
}

void LibrarySearchView::activate(QTreeWidgetItem* item)
{
    if (!item || !item->parent()) return;
    emit openRequested(item->data(0, BookIdRole).toInt(), item->data(0, PathRole).toString(), item->data(0, SpineRole).toInt(),
                       item->data(0, StartRole).toInt(), item->data(0, EndRole).toInt());
}

void LibrarySearchView::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (isSearching()) cancel();
        else emit backRequested();
        return;
    }
    QWidget::keyPressEvent(e);
}

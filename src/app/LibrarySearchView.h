// Search All Books (Go > Search All Books…, Ctrl+Shift+F, or the link next to the library's search box): a word
// or phrase in the text of every book in the library. Results come in book by book, grouped under each book;
// clicking one opens the book at that place.
#pragma once

#include "Theme.h"

#include "core/services/LibrarySearch.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace core {
class Library;
}

class LibrarySearchView : public QWidget {
    Q_OBJECT
public:
    LibrarySearchView(core::Library* library, QWidget* parent = nullptr);
    void setTheme(const Theme& t);

    void setQuery(const QString& query);   // fills the box and searches
    void search();                         // searches for what is in the box
    void cancel();
    bool isSearching() const;
    QTreeWidget* results() const { return m_results; }
    QString status() const;
    void activate(QTreeWidgetItem* item);  // a result: open its book there (tests)

signals:
    void backRequested();
    void openRequested(int bookId, const QString& path, int spine, int start, int end);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    void addBook(const core::LibraryBookHits& r, int done, int total);

    core::Library* m_library;
    core::LibrarySearch m_search;
    int m_searchId = 0;
    QString m_query;
    QToolButton* m_back;
    QLineEdit* m_edit;
    QPushButton* m_go;
    QCheckBox* m_whole;
    QCheckBox* m_case;
    QCheckBox* m_accents;
    QLabel* m_status;
    QTreeWidget* m_results;
    int m_unreadable = 0;
};

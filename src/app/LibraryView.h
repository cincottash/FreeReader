// The library (the approved cover-grid mock-up): search, Add Books, filters (Library, Languages, Collections),
// sorting, a cover grid or a list, and Continue Reading. Book files stay where they are; a book whose file has
// moved shows "File Not Found" with Locate….
#pragma once

#include "Theme.h"

#include "core/services/Library.h"

#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QStandardItemModel;
class QToolButton;
class QTreeView;
struct Settings;

namespace core {
class Importer;
}

class BookFilter;

class LibraryView : public QWidget {
    Q_OBJECT
public:
    using Prompt = std::function<void(const QString& title, const QString& placeholder, std::function<QString(const QString&)> accept)>;
    using Confirm = std::function<bool(const QString& title, const QString& text)>;

    LibraryView(core::Library* library, Settings& settings, QWidget* parent = nullptr);
    ~LibraryView() override;

    void refresh();                                  // re-reads the library
    void setTheme(const Theme& t);
    void setPrompt(Prompt p) { m_prompt = std::move(p); }
    void setConfirm(Confirm c) { m_confirm = std::move(c); }
    void setSettingsNotKept(bool on);
    void setMessage(const QString& text);            // a status or error line ("Couldn't open …")
    QString message() const;

    void addBooks(const QStringList& filesOrFolders);   // background import
    void chooseFilesToAdd();
    void chooseFolderToAdd();
    void newCollection();
    bool isImporting() const;

    // Filters: "all", "reading", "finished", "notstarted", "lang:es", "collection:3".
    void setFilter(const QString& key);
    QString filter() const { return m_filter; }
    void setSearch(const QString& text);
    void setSort(const QString& key);                // recent, title, author, language, progress, added
    void setListMode(bool list);
    QList<int> visibleBookIds() const;               // in display order
    void openBookById(int id);
    void locateBook(int id, const QString& newPath); // after "File Not Found"
    void removeBook(int id);
    void addToCollection(int collectionId, const QList<int>& bookIds);
    void setFinished(int id, bool finished);

    QListView* grid() const { return m_grid; }
    QTreeView* list() const { return m_list; }
    QListWidget* filters() const { return m_filters; }

signals:
    void openRequested(const QString& path);
    void settingsLinkClicked();
    void commandsRequested();

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void rebuildFilters();
    void updateHeading();
    void updateContinue();
    void showBookMenu(const QPoint& globalPos, const QModelIndex& index);
    void showCollectionMenu(const QPoint& globalPos, QListWidgetItem* item);
    int bookIdAt(const QModelIndex& index) const;
    const core::LibraryBook* bookById(int id) const;
    void activate(const QModelIndex& index);

    core::Library* m_library;
    Settings& m_settings;
    Theme m_theme = Theme::midnight();
    core::Importer* m_importer;
    Prompt m_prompt;
    Confirm m_confirm;
    QList<core::LibraryBook> m_books;
    QHash<int, QSet<int>> m_members;
    QString m_filter = QStringLiteral("all");

    QLineEdit* m_search;
    QPushButton* m_add;
    QToolButton* m_commands;
    QPushButton* m_notKept;
    QListWidget* m_filters;
    QLabel* m_heading;
    QComboBox* m_sort;
    QToolButton* m_gridButton;
    QToolButton* m_listButton;
    QStackedWidget* m_views;
    QListView* m_grid;
    QTreeView* m_list;
    QLabel* m_empty;
    QLabel* m_message;
    QWidget* m_continueBar;
    QLabel* m_continueText;
    QPushButton* m_continue;
    int m_continueId = 0;
    QStandardItemModel* m_model;
    BookFilter* m_proxy;
};

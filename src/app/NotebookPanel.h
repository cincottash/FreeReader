// The Notebook: the book's highlights, notes and bookmarks on the right of the page, in reading order, with a
// filter (All, Highlights, Notes, Bookmarks, or one color). Clicking one goes there; right-click edits the
// note, changes the color or deletes it. Export… writes them as Markdown.
#pragma once

#include "Theme.h"

#include "core/services/Annotations.h"

#include <QFrame>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class NotebookPanel : public QFrame {
    Q_OBJECT
public:
    explicit NotebookPanel(QWidget* parent = nullptr);
    void setTheme(const Theme& t);
    void setAnnotations(const QList<core::Annotation>& list);
    QListWidget* list() const { return m_list; }
    QString filter() const;
    void setFilter(const QString& key);   // all, highlights, notes, bookmarks, or a color key

signals:
    void activated(int annotationId);
    void editNoteRequested(int annotationId);
    void colorRequested(int annotationId, const QString& color);
    void deleteRequested(int annotationId);
    void exportRequested();

private:
    void fill();

    Theme m_theme = Theme::midnight();
    QList<core::Annotation> m_all;
    QComboBox* m_filter;
    QListWidget* m_list;
    QLabel* m_empty;
    QPushButton* m_export;
};

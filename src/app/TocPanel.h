// The table of contents panel: nested parts and chapters, the current one marked.
#pragma once

#include "Theme.h"

#include <QWidget>

#include <vector>

class QTreeWidget;
class QTreeWidgetItem;

namespace core {
struct TocEntry;
}

class TocPanel : public QWidget {
    Q_OBJECT
public:
    explicit TocPanel(QWidget* parent = nullptr);
    void setTheme(const Theme& t);
    void setEntries(const std::vector<core::TocEntry>& toc, const std::vector<bool>& reachable);
    void setCurrent(int entry);
    int current() const { return m_current; }
    QTreeWidget* tree() const { return m_tree; }

signals:
    void entryActivated(int entry);

private:
    QTreeWidget* m_tree;
    class QLabel* m_none;
    std::vector<QTreeWidgetItem*> m_items;
    int m_current = -1;
    Theme m_theme = Theme::midnight();
};

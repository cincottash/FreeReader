// Search This Book: a box, two options (Whole Words, Match Accents), and the results in context with their page
// numbers. Typing searches after a short pause; Enter searches at once. Clicking a result goes there and selects
// the match.
#pragma once

#include "core/services/BookSearch.h"

#include <QFrame>
#include <QTimer>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;

class SearchPanel : public QFrame {
    Q_OBJECT
public:
    explicit SearchPanel(QWidget* parent = nullptr);
    void setQuery(const QString& q);      // and searches
    QString query() const;
    core::SearchOptions options() const;
    void setSearching();
    void setResults(const QList<core::SearchHit>& hits, const QStringList& pageLabels, bool truncated);
    const QList<core::SearchHit>& results() const { return m_hits; }
    QListWidget* list() const { return m_list; }
    QLineEdit* edit() const { return m_edit; }
    void focusBox();

signals:
    void searchRequested(const QString& query, const core::SearchOptions& options);
    void hitActivated(int index);
    void closeRequested();

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    void start();

    QLineEdit* m_edit;
    QCheckBox* m_whole;
    QCheckBox* m_accents;
    QLabel* m_status;
    QListWidget* m_list;
    QTimer m_typing;
    QList<core::SearchHit> m_hits;
};

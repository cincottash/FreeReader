// The theme list: Reading Themes, Dark Themes and Light Themes (each alphabetical), then Match Operating System.
// Pointing at a theme previews it on the window behind the list; clicking keeps it; closing restores the saved one.
#pragma once

#include <QFrame>
#include <QTimer>

class QListWidget;
class QListWidgetItem;

class ThemePicker : public QFrame {
    Q_OBJECT
public:
    explicit ThemePicker(QWidget* parent);
    void showFor(const QString& currentKey, const QPoint& topRight);
    QListWidget* list() const { return m_list; }

signals:
    void previewRequested(const QString& key);   // empty: show the saved theme again
    void chosen(const QString& key);

protected:
    void keyPressEvent(QKeyEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    void fill(const QString& currentKey);
    void hoverItem(QListWidgetItem* it);

    QListWidget* m_list;
    QTimer m_previewTimer;
    QString m_pending;
};

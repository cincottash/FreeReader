// The start screen until the library view arrives in phase 2: open a book, drop one, or continue a recent one.
#pragma once

#include "Theme.h"

#include "core/services/Library.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

class StartView : public QWidget {
    Q_OBJECT
public:
    explicit StartView(QWidget* parent = nullptr);
    void setRecent(const QList<core::LibraryBook>& books);
    void setMessage(const QString& text);   // an error or status line under the buttons (empty hides it)
    void setSettingsNotKept(bool on);

signals:
    void openRequested();
    void bookRequested(const QString& path);
    void settingsLinkClicked();

private:
    QLabel* m_message;
    QLabel* m_recentTitle;
    QListWidget* m_recent;
    QPushButton* m_notKept;
};

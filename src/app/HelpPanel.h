// Help > Getting Started: a guide in a panel on the window's right (in the library and while reading), like the
// practice workspace. It shows every feature and how to use it, how to change the common settings, and answers to
// common questions. Links in it run the command they name ("Open Audio Settings"), so a setting is one click away.
#pragma once

#include "Theme.h"

#include <QFrame>
#include <QStringList>

class QTextBrowser;
class QToolButton;
class QLineEdit;

class HelpPanel : public QFrame {
    Q_OBJECT
public:
    explicit HelpPanel(QWidget* parent = nullptr);
    void setTheme(const Theme& t);
    void showSection(const QString& anchor);   // "reading", "translation", "faq"…; empty: the top
    QStringList sections() const;              // the anchors, in order
    QString text() const;                      // the guide as plain text (tests)
    QTextBrowser* browser() const { return m_browser; }
    // Finds the next place the words appear (the search box); false when they don't.
    bool find(const QString& words);
    void clickLink(const QString& href);       // what a click on a link does (tests)

signals:
    void closeRequested();
    void commandRequested(const QString& id);   // a "run:<command>" link

private:
    QString html() const;
    QTextBrowser* m_browser;
    QToolButton* m_close;
    QLineEdit* m_search;
    Theme m_theme = Theme::midnight();
};

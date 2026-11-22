// The bilingual view's panel, on the right of the page: the English of every paragraph on the screen, in the
// same order, each as its own block (a paragraph that began on the previous page included). Paragraphs still
// being translated show as "…"; a message replaces the list when nothing can translate.
#pragma once

#include "Theme.h"

#include <QFrame>
#include <QStringList>

class QLabel;
class QScrollArea;
class QVBoxLayout;

class BilingualPanel : public QFrame {
    Q_OBJECT
public:
    explicit BilingualPanel(QWidget* parent = nullptr);
    void setTheme(const Theme& t);
    // One entry per paragraph on the screen; an empty one is still being translated.
    void setParagraphs(const QStringList& english, const QString& status = QString());
    void setMessage(const QString& message);   // nothing to show, and why
    QStringList paragraphs() const { return m_english; }
    QString status() const;

signals:
    void closeRequested();

private:
    Theme m_theme = Theme::midnight();
    QStringList m_english;
    QLabel* m_status;
    QScrollArea* m_scroll;
    QWidget* m_list;
    QVBoxLayout* m_listLay;
};

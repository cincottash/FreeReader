// File > Import Kindle Highlights…: what was found in the open book, what to bring in (highlights, words for the
// Word List, the words this edition words differently, bookmarks, the Kindle reading place), and the result.
#pragma once

#include "ReaderView.h"

#include "core/services/KindleImport.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;

class KindleImportDialog : public QDialog {
    Q_OBJECT
public:
    using Apply = std::function<ReaderView::KindleResult(const ReaderView::KindleOptions&)>;
    KindleImportDialog(const core::KindleExport& ex, const core::KindlePlan& plan, const QString& bookTitle, Apply apply, QWidget* parent);

    ReaderView::KindleOptions options() const;
    void runImport();                         // the Import button
    QString summary() const;                  // what was found
    QString resultText() const;               // after Import
    bool imported() const { return m_done; }

private:
    Apply m_apply;
    QLabel* m_summary;
    QLabel* m_result;
    QCheckBox *m_highlights, *m_words, *m_missing, *m_passages, *m_bookmarks, *m_move;
    QPushButton* m_showMissing;
    QListWidget* m_missingList;
    QPushButton* m_import;
    QPushButton* m_close;
    bool m_done = false;
};

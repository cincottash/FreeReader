// The reading screen: header (back to the library, title and chapter, contents, commands, full screen), the
// contents panel, the pages, the progress footer, Back after a jump, and footnote pop-ups.
#pragma once

#include "PageView.h"
#include "Theme.h"

#include "core/position/Position.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <vector>

class FooterBar;
class NotePopup;
class QLabel;
class QPushButton;
class QToolButton;
class TocPanel;
struct Settings;

namespace core {
class BookEngine;
class Library;
}

class ReaderView : public QWidget {
    Q_OBJECT
public:
    ReaderView(core::BookEngine* engine, core::Library* library, Settings& settings, QWidget* parent = nullptr);

    void showBook(int bookId, const core::Position& start);
    void closeBook();            // saves the position
    void applyReadingOptions();  // text size, spacing, columns from the settings
    void setSettingsNotKept(bool on);
    void setInteractive(bool on) { m_interactive = on; }   // off: never opens the browser (tests, screenshots)

    PageView* pages() const { return m_pages; }
    TocPanel* contents() const { return m_toc; }
    FooterBar* footer() const { return m_footer; }
    NotePopup* notePopup() const { return m_note; }
    bool contentsVisible() const;
    void setContentsVisible(bool on);
    bool isSettled() const;      // the book, its index and the current page are ready

    // Navigation
    void jumpTo(const core::Position& p);   // remembers where it came from (Back)
    bool canGoBack() const { return !m_history.empty(); }
    void goBack();
    void nextChapter();
    void previousChapter();
    QString goToPageOrPercent(const QString& text);   // empty on success, else a message for the user
    void setFullScreenIcon(bool fullScreen);

signals:
    void libraryRequested();
    void commandsRequested();
    void fullScreenRequested();
    void imageRequested(const QString& path);
    void settingsLinkClicked();

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    void onPositionChanged(const core::Position& p, bool byTurn);
    void onIndexReady();
    void updateHeaderAndFooter();
    void onLink(const LinkClick& link, const QPoint& globalPos);
    void savePosition();
    void placeBackChip();
    QString chapterTitleAt(const core::Position& p) const;

    core::BookEngine* m_engine;
    core::Library* m_library;
    Settings& m_settings;
    Theme m_theme = Theme::midnight();
    int m_bookId = 0;
    bool m_interactive = true;

    QToolButton* m_libraryButton;
    QLabel* m_title;
    QLabel* m_subtitle;
    QPushButton* m_notKept;
    QToolButton* m_tocButton;
    QToolButton* m_commandsButton;
    QToolButton* m_fullButton;
    TocPanel* m_toc;
    PageView* m_pages;
    FooterBar* m_footer;
    QPushButton* m_backChip;
    NotePopup* m_note;

    std::vector<core::Position> m_history;
    QTimer m_saveTimer;
    QElapsedTimer m_sinceTurn;
    core::Position m_lastShown;
    int m_lastScreenChars = 0;
};

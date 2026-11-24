// The Dictionary card (right-click > Dictionary): a word's whole dictionary entry in the colors and type of the page,
// laid out like a printed dictionary (as in Kindle's): the dictionary's name, the headword and its pronunciation,
// its forms and origin, then each part of speech (I., II., ...) with numbered senses, their labels and examples.
// Below: Full Definition (the entry on Wiktionary, in the browser) and Translate (the translation pop-up). It never
// takes the keyboard focus, so the arrow keys keep turning pages.
#pragma once

#include "Theme.h"

#include "core/translate/OfflineDictionary.h"

#include <QFrame>
#include <QUrl>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class DictionaryPopup : public QFrame {
    Q_OBJECT
public:
    explicit DictionaryPopup(QWidget* parent);
    void setTheme(const Theme& t);

    // `title`: the dictionary's name. `fullDefinition`: the entry online. `offerDefinitions`: the card comes from a
    // translating dictionary and a dictionary of definitions can be downloaded (a link to Settings says so).
    void showCard(const core::DictionaryCard& card, const QString& title, const QString& source, const QUrl& fullDefinition,
                  const QRect& anchor, bool offerDefinitions);
    // Two sides (the owner's request, 2026-11-23): `front` from a dictionary of definitions in the book's language,
    // `back` from a translating one (Spanish -> English). Flip, at the bottom right, turns the card over.
    struct Side {
        core::DictionaryCard card;
        QString title, source;
        QUrl fullDefinition;
        QString language;   // what the side is written in: "es", "en"
    };
    void showSides(const Side& front, const Side& back, const QRect& anchor);
    bool hasTwoSides() const { return m_twoSides; }
    bool showingFront() const { return m_front; }
    void flipSide();
    const core::DictionaryCard& card() const { return m_card; }
    QString html() const { return m_html; }
    QUrl fullDefinitionUrl() const { return m_url; }

    // The card as rich text (tests read it; the label shows it).
    static QString toHtml(const core::DictionaryCard& card, const Theme& theme);
    // Highlight (beside Translate): the word in the color used last; Remove Highlight once it is.
    void setHighlight(bool available, bool highlighted, const QColor& color);

signals:
    void translateRequested();
    void highlightRequested();
    void fullDefinitionRequested(const QUrl& url);
    void settingsRequested();

private:
    void place();
    void showSide(const Side& s);

    bool m_twoSides = false, m_front = true;
    Side m_sides[2];
    QPushButton* m_flip;

    Theme m_theme = Theme::midnight();
    core::DictionaryCard m_card;
    QString m_html;
    QUrl m_url;
    QRect m_anchor;

    QVBoxLayout* m_lay;
    QLabel* m_title;
    QScrollArea* m_scroll;
    QLabel* m_body;
    QPushButton* m_full;
    QPushButton* m_translate;
    QPushButton* m_highlight;
    QPushButton* m_getDefinitions;
    QLabel* m_source;
};

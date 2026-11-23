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
    const core::DictionaryCard& card() const { return m_card; }
    QString html() const { return m_html; }
    QUrl fullDefinitionUrl() const { return m_url; }

    // The card as rich text (tests read it; the label shows it).
    static QString toHtml(const core::DictionaryCard& card, const Theme& theme);

signals:
    void translateRequested();
    void fullDefinitionRequested(const QUrl& url);
    void settingsRequested();

private:
    void place();

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
    QPushButton* m_getDefinitions;
    QLabel* m_source;
};

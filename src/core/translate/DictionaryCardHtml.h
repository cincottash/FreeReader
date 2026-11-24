// The Dictionary card as rich text, for the desktop's card and the touch app's: the headword and its pronunciation,
// what form the selected word is, forms and origin, then each part of speech (I., II., ...) with numbered senses,
// their labels and examples. Everything from the dictionary file is escaped.
#pragma once

#include "OfflineDictionary.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QUrl>

namespace core {

// The card for a word in a book: the dictionary that answers it and what the card says about that dictionary.
struct FoundCard {
    const OfflineDictionary* dictionary = nullptr;   // null: no dictionary (on) knows the word
    DictionaryCard card;
    QString title;          // the dictionary's name
    QString source;         // "Source: Wiktionary (Spanish edition) (CC BY-SA 4.0 and GFDL)"
    QUrl fullDefinition;    // the entry on Wiktionary
    bool offerDefinitions = false;   // answered by a translating dictionary; a Spanish one of definitions exists
    bool found() const { return dictionary && !card.isEmpty(); }
};

// A dictionary of definitions in the book's language first (Spanish for a Spanish book), else a translating one
// (Spanish -> English). Dictionaries that aren't open or are switched off are skipped.
// `kind`: only a dictionary of definitions, or only a translating one (the two sides of the Dictionary card).
enum class DictionaryKind { Any, Definitions, Translating };
FoundCard findDictionaryCard(const QList<OfflineDictionary*>& dictionaries, const QString& text, const QString& language,
                             DictionaryKind kind = DictionaryKind::Any);

QString dictionaryCardHtml(const DictionaryCard& card, const QColor& fg, const QColor& muted, const QColor& line);

}  // namespace core

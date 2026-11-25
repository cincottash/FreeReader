// The Dictionary card's content, laid out like a printed dictionary: each headword, its forms and origin, then its
// parts of speech, each with numbered senses, their labels ("coloquial", "México") and examples. The offline
// dictionaries make one from their entries; Gemini and LM Studio answer a word with one (two: one in the reader's
// language, one in the book's), so every word looked up is shown the same way (the owner's request, 2026-11-25).
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace core {

struct DictionarySense {
    QString gloss;
    QStringList labels, examples;
};
struct DictionaryEntry {
    QString partOfSpeech;   // "Adjetivo", "noun, feminine"
    QString origin;         // "Del latín tranquillum."
    QString forms;          // "tranquilo, tranquila, tranquilos"
    QString pronunciation;  // IPA
    QList<DictionarySense> senses;
};
struct DictionaryHeadword {
    QString word;
    QString formOf;         // the selected word, when it's a form of this one ("dijo")
    QString formNote;       // what form it is ("Primera persona del singular ... de decir"), when known
    QList<DictionaryEntry> entries;
};
struct DictionaryCard {
    QString text;           // the word asked for (punctuation trimmed)
    QList<DictionaryHeadword> headwords;
    bool isEmpty() const { return headwords.isEmpty(); }
};

// The card as JSON (kept with an AI's answer in the translation cache) and back.
QJsonObject cardToJson(const DictionaryCard& card);
DictionaryCard cardFromJson(const QJsonObject& o);

// The cards in an AI's answer ("entries", see ai::userPrompt): `bookSide` false gives the one in the reader's
// language (glosses), true the one in the book's (definitions). `text` is the word asked for.
DictionaryCard cardFromAiEntries(const QJsonArray& entries, const QString& text, bool bookSide);

}  // namespace core

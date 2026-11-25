// The offline dictionary: a bilingual word list in an SQLite file (made from Wiktionary data by a converter
// script), used when Gemini and LM Studio can't answer. Inflected forms point to their dictionary form, so
// "tenía" finds "tener" with "first/third-person singular imperfect indicative".
//
// File layout (SQLite):
//   meta(key TEXT PRIMARY KEY, value TEXT)            -- language, target, source, license, built
//   entries(word TEXT, folded TEXT, pos TEXT, tags TEXT, glosses TEXT)   -- glosses: JSON array of strings
//   forms(form TEXT, folded TEXT, lemma TEXT, pos TEXT, description TEXT)
// A dictionary of definitions may have one more column, entries.card: the whole entry as JSON
// {pos, etymology, forms, ipa, senses: [{gloss, tags, examples}]}, for the Dictionary card. Without it the card
// is made from the glosses.
#pragma once

#include "Translation.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace core {

struct DictionaryInfo {
    bool ok = false;
    QString language, target, source, license, built;
    QString title;          // the dictionary's own name ("Diccionario de español (Wiktionary)"), when it has one
    bool cards = false;     // it has whole entries (a `card` column) for the Dictionary card
    int entries = 0;
    QString error;
};

// The Dictionary card's content (DictionarySense, DictionaryEntry, DictionaryHeadword, DictionaryCard) is in
// DictionaryCard.h, shared with the AI providers' answers.

class OfflineDictionary : public TranslationProvider {
    Q_OBJECT
public:
    explicit OfflineDictionary(QObject* parent = nullptr);
    ~OfflineDictionary() override;

    // Opens a dictionary file; false (and info().error) when it isn't one.
    bool open(const QString& path);
    void close();
    const DictionaryInfo& info() const { return m_info; }
    QString path() const { return m_path; }
    static DictionaryInfo inspect(const QString& path);

    QString name() const override { return providerName(); }
    static QString providerName() { return tr("Offline dictionary"); }   // its answers' `provider`
    bool isSetUp() const override { return m_info.ok && m_enabled; }
    // Only words in its own language (a Spanish book never asks the English dictionary).
    bool handles(const TranslationRequest& request) const override;
    bool cacheable() const override { return false; }
    // A dictionary of definitions in the same language (English -> English), not a translating one.
    bool isMonolingual() const { return !m_info.target.isEmpty() && m_info.target == m_info.language; }
    void setEnabled(bool on) { m_enabled = on; }
    void translate(const TranslationRequest& request, Done done) override;

    // The lookup itself, synchronous (tests).
    TranslationResult lookUp(const QString& text, const QString& language) const;
    // The whole entries for a word (the word itself, and the words it is a form of), for the Dictionary card.
    // Empty when the dictionary isn't open or doesn't know the word.
    DictionaryCard card(const QString& text) const;

private:
    QString m_connection;
    QString m_path;
    DictionaryInfo m_info;
    bool m_enabled = true;
};

}  // namespace core

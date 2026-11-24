// Language packs: everything FreeReader needs to read books in one language as a speaker of another, downloaded
// together so the app itself stays small. A pack holds the language's offline dictionary into the reader's language,
// its own dictionary of definitions (the Dictionary card) and the offline sentence translator's model; Gemini and LM
// Studio need nothing downloaded.
//
// Only complete packs are offered (the owner's rule, 2026-11-24: "I don't want to have any language that only has
// partial language support"): a language is listed only when every part exists for it. Adding a language is adding
// its pack here, once its downloads are published.
#pragma once

#include "DictionaryDownload.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace core::languages {

struct PackPart {
    enum Kind { Dictionary, Definitions, Translator };
    Kind kind = Dictionary;
    QString key;                       // the dictionary's key ("es", "es-es", "en") or "translator"
    DictionaryDownload::Source source;
};

struct LanguagePack {
    QString language;                  // the books' language ("es")
    QString reader;                    // the reader's own language ("en")
    QList<PackPart> parts;
    qint64 downloadSize() const;
    // "Spanish dictionary, Spanish definitions and the offline translator"
    QString contents() const;
};

// The languages FreeReader can be used in (its own text, the answers of every engine): English for now.
QStringList readerLanguages();
// The complete packs for a reader: each language that can be read with every part (English: Spanish, and English
// definitions for English books).
QList<LanguagePack> packsFor(const QString& reader);
std::optional<LanguagePack> packFor(const QString& reader, const QString& language);

// The language's name in English ("Spanish") and in itself ("Español").
QString englishName(const QString& code);
QString nativeName(const QString& code);

// The reader's language for this run (Settings > General > Your Language), "en" until set. The engines answer in
// it and the pop-ups label it.
QString readerLanguage();
void setReaderLanguage(const QString& code);

}  // namespace core::languages

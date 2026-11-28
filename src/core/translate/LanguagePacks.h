// Language packs: everything Biblioglot needs to read books in one language as a speaker of another, downloaded
// together so the app itself stays small. A pack holds the language's offline dictionary into the reader's language,
// its own dictionary of definitions (the Dictionary card) and the offline sentence translator with its model; Gemini
// and LM Studio need nothing downloaded.
//
// Only complete packs are offered (the owner's rule, 2026-11-24: "I don't want to have any language that only has
// partial language support"): a language is listed only when every part exists for it, in both directions it is
// offered. Adding a language is adding its pack here, once its downloads are published. Nothing is downloaded until
// the reader asks (the welcome on the first start, or Settings > General).
#pragma once

#include "DictionaryDownload.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace core::languages {

struct PackPart {
    enum Kind { Dictionary, Definitions, Translator, TranslatorModel };
    Kind kind = Dictionary;
    // The dictionary's key ("es", "en-es", "es-es", "en"), "translator", or "model-<pair>" ("model-en-es").
    QString key;
    DictionaryDownload::Source source;
};

struct LanguagePack {
    QString language;                  // the books' language ("es")
    QString reader;                    // the reader's own language ("en")
    QList<PackPart> parts;
    qint64 downloadSize() const;
    // "Spanish → English dictionary, Spanish definitions and the offline translator", in the interface's language.
    QString contents() const;
};

// The languages Biblioglot can be used in (its own text, the answers of every engine): English and Spanish.
QStringList readerLanguages();
// The complete packs for a reader: each language that can be read with every part (English: Spanish, and English
// definitions for English books; Spanish: English, and Spanish definitions).
QList<LanguagePack> packsFor(const QString& reader);
std::optional<LanguagePack> packFor(const QString& reader, const QString& language);

// The language's name in English ("Spanish"), in itself ("Español"), and in the interface's language ("Spanish" in
// English, "español" in Spanish).
QString englishName(const QString& code);
QString nativeName(const QString& code);
QString displayName(const QString& code);
QString displayTitle(const QString& code);   // the same with a capital, to start a label ("Español")

// The reader's language for this run (Settings > General > Language), "en" until set. The engines answer in it, the
// pop-ups label it and the app's own text is in it.
QString readerLanguage();
void setReaderLanguage(const QString& code);
// The language to suggest on the first start: this computer's, when Biblioglot can be used in it.
QString suggestedReaderLanguage();

}  // namespace core::languages

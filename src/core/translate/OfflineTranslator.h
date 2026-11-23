// The offline sentence translator: Mozilla's Bergamot (the engine Firefox translates with), downloaded on request
// as a small separate program with its Spanish -> English model. FreeReader starts the program for each lookup,
// sends the text and reads the English back (about 0.15 s for a sentence, 0.3 s for a page of paragraphs, on one
// CPU thread). Used for sentences and passages, and for the bilingual view's paragraphs, when Gemini and LM Studio
// can't answer; never for Explain This Sentence (it translates, it doesn't explain).
//
// Its folder (in the app data folder): the program (freereader-translator[.exe]), and one folder per language
// pair ("es-en") with model.*.intgemm.alphas.bin, lex.*.bin, vocab.*.spm and nonbreaking_prefix.<lang>.
#pragma once

#include "Translation.h"

#include <QString>

namespace core {

class OfflineTranslator : public TranslationProvider {
    Q_OBJECT
public:
    explicit OfflineTranslator(QObject* parent = nullptr);

    // The folder the pack was unpacked into; looks for the program and the model there.
    void setFolder(const QString& folder);
    QString folder() const { return m_folder; }
    bool isInstalled() const;                   // the program and a Spanish -> English model are there
    QString program() const;
    void setEnabled(bool on) { m_enabled = on; }

    QString name() const override { return tr("Offline translator"); }
    bool isSetUp() const override { return m_enabled && isInstalled(); }
    bool handles(const TranslationRequest& request) const override;
    bool cacheable() const override { return false; }
    void translate(const TranslationRequest& request, Done done) override;

    // The settings file Bergamot reads for a language pair (written next to the model). Empty when not installed.
    QString configFor(const QString& from) const;

private:
    QString m_folder;
    bool m_enabled = true;
};

}  // namespace core

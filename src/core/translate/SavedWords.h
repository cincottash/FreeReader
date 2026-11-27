// Answers from the Word List: a word looked up (or highlighted and given a meaning) before, in any sentence, is
// answered with the meaning saved then, with no internet. Asked last of all, after the offline dictionaries and
// translator: the saved meaning was right for the sentence it was looked up in, often not for this one (the owner's
// rule, 2026-11-27).
#pragma once

#include "Translation.h"

namespace core {

class WordLog;
class OfflineDictionary;

class SavedWords : public TranslationProvider {
    Q_OBJECT
public:
    explicit SavedWords(QObject* parent = nullptr);
    void setWordLog(WordLog* log) { m_log = log; }
    // For the dictionary form of a word read in another form ("tengo" -> "tener").
    void setDictionaries(const std::vector<OfflineDictionary*>& dictionaries) { m_dictionaries = dictionaries; }

    QString name() const override { return tr("Your Word List"); }
    bool isSetUp() const override { return m_log != nullptr; }
    bool handles(const TranslationRequest& request) const override;
    bool cacheable() const override { return false; }
    bool reportsMisses() const override { return false; }
    void translate(const TranslationRequest& request, Done done) override;

    TranslationResult lookUp(const QString& text, const QString& language) const;   // synchronous (tests)

private:
    WordLog* m_log = nullptr;
    std::vector<OfflineDictionary*> m_dictionaries;
};

}  // namespace core

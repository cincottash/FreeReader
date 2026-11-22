// The offline dictionary: a bilingual word list in an SQLite file (made from Wiktionary data by a converter
// script), used when Gemini and LM Studio can't answer. Inflected forms point to their dictionary form, so
// "tenía" finds "tener" with "first/third-person singular imperfect indicative".
//
// File layout (SQLite):
//   meta(key TEXT PRIMARY KEY, value TEXT)            -- language, target, source, license, built
//   entries(word TEXT, folded TEXT, pos TEXT, tags TEXT, glosses TEXT)   -- glosses: JSON array of strings
//   forms(form TEXT, folded TEXT, lemma TEXT, pos TEXT, description TEXT)
#pragma once

#include "Translation.h"

#include <QHash>
#include <QString>

namespace core {

struct DictionaryInfo {
    bool ok = false;
    QString language, target, source, license, built;
    int entries = 0;
    QString error;
};

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

    QString name() const override { return tr("Offline dictionary"); }
    bool isSetUp() const override { return m_info.ok && m_enabled; }
    void setEnabled(bool on) { m_enabled = on; }
    void translate(const TranslationRequest& request, Done done) override;

    // The lookup itself, synchronous (tests).
    TranslationResult lookUp(const QString& text, const QString& language) const;

private:
    QString m_connection;
    QString m_path;
    DictionaryInfo m_info;
    bool m_enabled = true;
};

}  // namespace core

#include "SavedWords.h"

#include "OfflineDictionary.h"
#include "core/services/WordLog.h"

#include <QRegularExpression>
#include <QTimer>

namespace core {

SavedWords::SavedWords(QObject* parent) : TranslationProvider(parent) {}

bool SavedWords::handles(const TranslationRequest& request) const
{
    // Words and short phrases, as the Word List has them.
    return request.kind == TranslationRequest::Kind::Lookup && request.text.simplified().split(QLatin1Char(' ')).size() <= 4;
}

TranslationResult SavedWords::lookUp(const QString& text, const QString& language) const
{
    TranslationResult r;
    if (!m_log) return r;
    QString word = text.trimmed();
    word.remove(QRegularExpression(QStringLiteral("^[\\p{P}\\s]+|[\\p{P}\\s]+$")));
    if (word.isEmpty()) return r;
    QString lemma;
    for (OfflineDictionary* d : m_dictionaries)
        if (d && d->info().ok && (language.isEmpty() || d->info().language == language)) {
            lemma = d->lookUp(word, language).lemma;
            if (!lemma.isEmpty()) break;
        }
    const WordEntry e = m_log->find(language, word, lemma);
    if (e.id == 0) return r;
    r.text = text.trimmed();
    r.translation = e.translation;
    r.alternatives = e.alternatives;
    r.partOfSpeech = e.partOfSpeech;
    r.lemma = e.lemma.compare(word, Qt::CaseInsensitive) == 0 ? QString() : e.lemma;
    r.definition = e.definition;
    r.note = e.userNote.isEmpty() ? e.note : e.userNote;
    r.provider = name();
    return r;
}

void SavedWords::translate(const TranslationRequest& request, Done done)
{
    const TranslationResult r = isSetUp() ? lookUp(request.text, request.from) : TranslationResult();
    const TranslationFailure f = !isSetUp() ? TranslationFailure::NotSetUp : r.isEmpty() ? TranslationFailure::NotFound : TranslationFailure::None;
    QTimer::singleShot(0, this, [done, r, f] { done(r, f, {}); });
}

}  // namespace core

#include "Translation.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>

namespace core {

bool TranslationRequest::isWord() const
{
    const QString t = text.trimmed();
    if (t.isEmpty() || t.size() > 40) return false;
    for (const QChar c : t)
        if (c.isSpace()) return false;
    return true;
}

QString TranslationRequest::cacheKey() const
{
    // The meaning of a word depends on its sentence, so both are part of the key.
    const QByteArray raw = (from + QLatin1Char('|') + to + QLatin1Char('|') + text.trimmed() + QLatin1Char('|') + sentence.simplified()).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
}

QJsonObject TranslationResult::toJson() const
{
    QJsonObject o;
    o[QLatin1String("text")] = text;
    o[QLatin1String("translation")] = translation;
    o[QLatin1String("alternatives")] = QJsonArray::fromStringList(alternatives);
    o[QLatin1String("part_of_speech")] = partOfSpeech;
    o[QLatin1String("lemma")] = lemma;
    o[QLatin1String("definition")] = definition;
    o[QLatin1String("in_sentence")] = inSentence;
    o[QLatin1String("note")] = note;
    o[QLatin1String("provider")] = provider;
    return o;
}

TranslationResult TranslationResult::fromJson(const QJsonObject& o)
{
    TranslationResult r;
    auto s = [&](const char* k) { return o.value(QLatin1String(k)).toString().trimmed(); };
    r.text = s("text");
    r.translation = s("translation");
    for (const QJsonValue& v : o.value(QLatin1String("alternatives")).toArray()) {
        const QString a = v.toString().trimmed();
        if (!a.isEmpty() && a.compare(r.translation, Qt::CaseInsensitive) != 0 && !r.alternatives.contains(a, Qt::CaseInsensitive))
            r.alternatives << a;
    }
    while (r.alternatives.size() > 6) r.alternatives.removeLast();
    r.partOfSpeech = s("part_of_speech");
    r.lemma = s("lemma");
    r.definition = s("definition");
    r.inSentence = s("in_sentence");
    r.note = s("note");
    r.provider = s("provider");
    return r;
}

QString failureText(TranslationFailure f)
{
    switch (f) {
    case TranslationFailure::None: return {};
    case TranslationFailure::NotSetUp: return QCoreApplication::translate("Translation", "not set up");
    case TranslationFailure::Offline: return QCoreApplication::translate("Translation", "not reachable");
    case TranslationFailure::DailyLimit: return QCoreApplication::translate("Translation", "today's free limit is used up");
    case TranslationFailure::RateLimit: return QCoreApplication::translate("Translation", "too many lookups this minute");
    case TranslationFailure::BadKey: return QCoreApplication::translate("Translation", "the key was refused");
    case TranslationFailure::NoModel: return QCoreApplication::translate("Translation", "no model loaded");
    case TranslationFailure::Failed: return QCoreApplication::translate("Translation", "no usable answer");
    }
    return {};
}

}  // namespace core

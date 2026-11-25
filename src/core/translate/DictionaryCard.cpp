#include "DictionaryCard.h"

namespace core {

namespace {

QJsonArray strings(const QStringList& list) { return QJsonArray::fromStringList(list); }

QStringList stringsOf(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& x : v.toArray()) {
        const QString s = x.toString().trimmed();
        if (!s.isEmpty()) out << s;
    }
    return out;
}

QString str(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toString().trimmed(); }

}  // namespace

QJsonObject cardToJson(const DictionaryCard& card)
{
    QJsonArray words;
    for (const DictionaryHeadword& w : card.headwords) {
        QJsonArray entries;
        for (const DictionaryEntry& e : w.entries) {
            QJsonArray senses;
            for (const DictionarySense& s : e.senses)
                senses.append(QJsonObject{{QStringLiteral("gloss"), s.gloss}, {QStringLiteral("labels"), strings(s.labels)},
                                          {QStringLiteral("examples"), strings(s.examples)}});
            entries.append(QJsonObject{{QStringLiteral("pos"), e.partOfSpeech}, {QStringLiteral("origin"), e.origin},
                                       {QStringLiteral("forms"), e.forms}, {QStringLiteral("ipa"), e.pronunciation},
                                       {QStringLiteral("senses"), senses}});
        }
        words.append(QJsonObject{{QStringLiteral("word"), w.word}, {QStringLiteral("form_of"), w.formOf},
                                 {QStringLiteral("form_note"), w.formNote}, {QStringLiteral("entries"), entries}});
    }
    return QJsonObject{{QStringLiteral("text"), card.text}, {QStringLiteral("headwords"), words}};
}

DictionaryCard cardFromJson(const QJsonObject& o)
{
    DictionaryCard c;
    c.text = str(o, "text");
    for (const QJsonValue& wv : o.value(QLatin1String("headwords")).toArray()) {
        const QJsonObject wo = wv.toObject();
        DictionaryHeadword w;
        w.word = str(wo, "word");
        w.formOf = str(wo, "form_of");
        w.formNote = str(wo, "form_note");
        for (const QJsonValue& ev : wo.value(QLatin1String("entries")).toArray()) {
            const QJsonObject eo = ev.toObject();
            DictionaryEntry e;
            e.partOfSpeech = str(eo, "pos");
            e.origin = str(eo, "origin");
            e.forms = str(eo, "forms");
            e.pronunciation = str(eo, "ipa");
            for (const QJsonValue& sv : eo.value(QLatin1String("senses")).toArray()) {
                const QJsonObject so = sv.toObject();
                DictionarySense s{str(so, "gloss"), stringsOf(so.value(QLatin1String("labels"))), stringsOf(so.value(QLatin1String("examples")))};
                if (!s.gloss.isEmpty()) e.senses << s;
            }
            if (!e.senses.isEmpty()) w.entries << e;
        }
        // A headword with no entries of its own is kept when it names the dictionary form ("tenía: a form of tener").
        if (!w.word.isEmpty() && (!w.entries.isEmpty() || !w.formOf.isEmpty())) c.headwords << w;
    }
    return c;
}

DictionaryCard cardFromAiEntries(const QJsonArray& entries, const QString& text, bool bookSide)
{
    DictionaryCard c;
    c.text = text.trimmed();
    for (const QJsonValue& hv : entries) {
        const QJsonObject ho = hv.toObject();
        DictionaryHeadword w;
        w.word = str(ho, "headword");
        if (w.word.isEmpty()) continue;
        w.formNote = str(ho, bookSide ? "form_note_native" : "form_note");
        // The selected word under its dictionary form ("piernas: plural of pierna").
        if (!w.formNote.isEmpty() && w.word.compare(c.text, Qt::CaseInsensitive) != 0) w.formOf = c.text;
        else w.formNote.clear();
        for (const QJsonValue& pv : ho.value(QLatin1String("parts")).toArray()) {
            const QJsonObject po = pv.toObject();
            DictionaryEntry e;
            e.partOfSpeech = str(po, bookSide ? "part_of_speech_native" : "part_of_speech");
            for (const QJsonValue& sv : po.value(QLatin1String("senses")).toArray()) {
                const QJsonObject so = sv.toObject();
                DictionarySense s;
                s.gloss = str(so, bookSide ? "definition" : "gloss");
                // Labels are in the reader's language: on that side only (each side all in one language).
                if (!bookSide) s.labels = stringsOf(so.value(QLatin1String("labels")));
                if (!s.gloss.isEmpty()) e.senses << s;
            }
            if (!e.senses.isEmpty()) w.entries << e;
        }
        if (!w.entries.isEmpty()) c.headwords << w;
    }
    return c;
}

}  // namespace core

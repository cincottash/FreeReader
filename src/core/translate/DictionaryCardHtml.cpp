#include "DictionaryCardHtml.h"

#include <QLocale>
#include <QObject>

namespace core {

namespace {

QString roman(int n)
{
    static const char* const numerals[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"};
    return n >= 1 && n <= 12 ? QString::fromLatin1(numerals[n - 1]) : QString::number(n);
}

QString esc(const QString& s) { return s.toHtmlEscaped(); }

// "Del latín tranquillum" -> "Del latín tranquillum."
QString sentence(QString s)
{
    s = s.trimmed();
    if (!s.isEmpty() && !QStringLiteral(".!?…").contains(s.back())) s += QLatin1Char('.');
    return s;
}

}  // namespace

QString dictionaryCardHtml(const DictionaryCard& card, const QColor& fg, const QColor& mutedColor, const QColor& line)
{
    const QString muted = mutedColor.name();
    QString h = QStringLiteral("<div style=\"font-family: 'Literata', Georgia, serif; font-size: 15px; color: %1;\">").arg(fg.name());
    for (int i = 0; i < card.headwords.size(); ++i) {
        const DictionaryHeadword& w = card.headwords[i];
        // A thin rule between headwords (Qt's <hr> ignores its color: a one-pixel table cell instead).
        if (i)
            h += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" style=\"margin: 10px 0 6px 0;\">"
                                "<tr><td bgcolor=\"%1\" height=\"1\" style=\"font-size: 1px;\"></td></tr></table>").arg(line.name());
        QString ipa;
        for (const DictionaryEntry& e : w.entries)
            if (!e.pronunciation.isEmpty()) {
                ipa = e.pronunciation;
                break;
            }
        h += QStringLiteral("<p style=\"margin: 2px 0 2px 0;\"><span style=\"font-size: 21px; font-weight: 600;\">%1</span>").arg(esc(w.word));
        if (!ipa.isEmpty()) h += QStringLiteral("&nbsp;&nbsp;<span style=\"color: %1;\">%2</span>").arg(muted, esc(ipa));
        h += QStringLiteral("</p>");
        if (!w.formOf.isEmpty())
            h += QStringLiteral("<p style=\"margin: 0 0 4px 0; color: %1;\"><i>%2</i>: %3</p>")
                     .arg(muted, esc(w.formOf), w.formNote.isEmpty() ? QObject::tr("a form of “%1”.").arg(esc(w.word)) : esc(sentence(w.formNote)));
        // Forms and origin once when every part of speech shares them (as a printed dictionary puts them first).
        QString forms, origin;
        bool sameOrigin = true;
        for (const DictionaryEntry& e : w.entries) {
            if (forms.isEmpty()) forms = e.forms;
            if (origin.isEmpty()) origin = e.origin;
            else if (!e.origin.isEmpty() && e.origin != origin) sameOrigin = false;
        }
        if (forms.contains(QLatin1Char(','))) h += QStringLiteral("<p style=\"margin: 2px 0;\">%1.").arg(esc(forms));
        else if (sameOrigin && !origin.isEmpty()) h += QStringLiteral("<p style=\"margin: 2px 0;\">");
        if (sameOrigin && !origin.isEmpty()) h += QStringLiteral(" <span style=\"color: %1;\">%2</span>").arg(muted, esc(sentence(origin)));
        if (forms.contains(QLatin1Char(',')) || (sameOrigin && !origin.isEmpty())) h += QStringLiteral("</p>");

        const bool numbered = w.entries.size() > 1;
        for (int j = 0; j < w.entries.size(); ++j) {
            const DictionaryEntry& e = w.entries[j];
            h += QStringLiteral("<p style=\"margin: 8px 0 2px 0;\">");
            if (numbered) h += QStringLiteral("<b>%1.</b> ").arg(roman(j + 1));
            h += QStringLiteral("<i>%1</i>").arg(esc(e.partOfSpeech));
            if (!sameOrigin && !e.origin.isEmpty()) h += QStringLiteral(" <span style=\"color: %1;\">%2</span>").arg(muted, esc(sentence(e.origin)));
            h += QStringLiteral("</p>");
            for (int k = 0; k < e.senses.size(); ++k) {
                const DictionarySense& s = e.senses[k];
                h += QStringLiteral("<p style=\"margin: 3px 0 3px 14px;\">");
                if (e.senses.size() > 1) h += QStringLiteral("<b>%1.</b> ").arg(k + 1);
                if (!s.labels.isEmpty()) h += QStringLiteral("<i style=\"color: %1;\">%2.</i> ").arg(muted, esc(s.labels.join(QStringLiteral(", "))));
                h += esc(s.gloss);
                for (const QString& x : s.examples)
                    h += QStringLiteral("<br/><span style=\"color: %1;\"><i>“%2”</i></span>").arg(muted, esc(x));
                h += QStringLiteral("</p>");
            }
        }
    }
    h += QStringLiteral("</div>");
    return h;
}

FoundCard findDictionaryCard(const QList<OfflineDictionary*>& dictionaries, const QString& text, const QString& language)
{
    FoundCard f;
    for (const bool monolingual : {true, false}) {
        for (const OfflineDictionary* d : dictionaries)
            if (d && d->isSetUp() && d->info().language == language && d->isMonolingual() == monolingual) {
                DictionaryCard c = d->card(text);
                if (!c.isEmpty()) {
                    f.dictionary = d;
                    f.card = c;
                    break;
                }
            }
        if (f.dictionary) break;
    }
    if (!f.found()) return f;
    const DictionaryInfo& info = f.dictionary->info();
    f.title = info.title;
    if (f.title.isEmpty()) {
        const QString name = QLocale::languageToString(QLocale(info.language).language());
        f.title = f.dictionary->isMonolingual()
                      ? QObject::tr("%1 Dictionary").arg(name)
                      : QObject::tr("%1–%2 Dictionary").arg(name, QLocale::languageToString(QLocale(info.target).language()));
    }
    // "Wiktionary (Spanish edition) via wiktextract / kaikki.org": the source itself, not how it was extracted.
    QString origin = info.source;
    if (const int via = int(origin.indexOf(QStringLiteral(" via "))); via > 0) origin = origin.left(via);
    f.source = QObject::tr("Source: %1").arg(info.license.isEmpty() ? origin : QObject::tr("%1 (%2)").arg(origin, info.license));
    // The entry on Wiktionary, in the dictionary's own language edition (the Spanish section of the English one for
    // a Spanish -> English dictionary).
    const QString edition = info.target.isEmpty() ? info.language : info.target;
    f.fullDefinition = QUrl(QStringLiteral("https://%1.wiktionary.org/wiki/").arg(edition) +
                            QString(f.card.headwords.first().word).replace(QLatin1Char(' '), QLatin1Char('_')));
    if (edition != info.language) f.fullDefinition.setFragment(QLocale::languageToString(QLocale(info.language).language()));
    f.offerDefinitions = !f.dictionary->isMonolingual() && language == QLatin1String("es");
    return f;
}

}  // namespace core

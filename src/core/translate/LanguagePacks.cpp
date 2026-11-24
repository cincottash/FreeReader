#include "LanguagePacks.h"

#include <QHash>
#include <QLocale>

namespace core::languages {

namespace {
QString g_reader = QStringLiteral("en");
}

qint64 LanguagePack::downloadSize() const
{
    qint64 n = 0;
    for (const PackPart& p : parts) n += p.source.size;
    return n;
}

QString LanguagePack::contents() const
{
    QStringList what;
    const QString name = englishName(language);
    for (const PackPart& p : parts) {
        if (p.kind == PackPart::Dictionary) what << QStringLiteral("%1 → %2 dictionary").arg(name, englishName(reader));
        else if (p.kind == PackPart::Definitions) what << QStringLiteral("%1 definitions").arg(name);
        else what << QStringLiteral("the offline translator");
    }
    if (what.size() <= 1) return what.join(QString());
    const QString last = what.takeLast();
    return what.join(QStringLiteral(", ")) + QStringLiteral(" and ") + last;
}

QStringList readerLanguages() { return {QStringLiteral("en")}; }

QList<LanguagePack> packsFor(const QString& reader)
{
    QList<LanguagePack> out;
    if (reader != QLatin1String("en")) return out;
    {
        LanguagePack es;
        es.language = QStringLiteral("es");
        es.reader = reader;
        es.parts << PackPart{PackPart::Dictionary, QStringLiteral("es"), DictionaryDownload::spanishEnglish()};
        es.parts << PackPart{PackPart::Definitions, QStringLiteral("es-es"), DictionaryDownload::spanishDefinitions()};
        const DictionaryDownload::Source t = DictionaryDownload::offlineTranslator();
        if (t.size > 0) es.parts << PackPart{PackPart::Translator, QStringLiteral("translator"), t};
        // Complete only with the translator: where no translator is built (another processor), Spanish isn't offered
        // as a pack (the dictionaries stay downloadable one by one in Settings > Translation).
        if (t.size > 0) out << es;
    }
    {
        LanguagePack en;   // English books for an English reader: definitions only (nothing to translate)
        en.language = QStringLiteral("en");
        en.reader = reader;
        en.parts << PackPart{PackPart::Definitions, QStringLiteral("en"), DictionaryDownload::englishDefinitions()};
        out << en;
    }
    return out;
}

std::optional<LanguagePack> packFor(const QString& reader, const QString& language)
{
    for (const LanguagePack& p : packsFor(reader))
        if (p.language == language) return p;
    return std::nullopt;
}

QString englishName(const QString& code)
{
    const QLocale l(code);
    return l.language() == QLocale::C ? code : QLocale::languageToString(l.language());
}

QString nativeName(const QString& code)
{
    // The languages FreeReader has hyphenation for, by name (Qt says "American English" for en).
    static const QHash<QString, QString> known = {{QStringLiteral("en"), QStringLiteral("English")}, {QStringLiteral("es"), QStringLiteral("Español")},
                                                  {QStringLiteral("fr"), QStringLiteral("Français")}, {QStringLiteral("de"), QStringLiteral("Deutsch")},
                                                  {QStringLiteral("it"), QStringLiteral("Italiano")}, {QStringLiteral("pt"), QStringLiteral("Português")}};
    if (known.contains(code)) return known.value(code);
    const QLocale l(code);
    QString n = l.nativeLanguageName();
    if (n.isEmpty()) return englishName(code);
    // "español de España", "English (United States)": the language alone, with a capital ("Español").
    if (n.contains(QLatin1Char('('))) n = n.left(n.indexOf(QLatin1Char('('))).trimmed();
    const QString territory = l.nativeTerritoryName();
    if (!territory.isEmpty() && n.endsWith(territory, Qt::CaseInsensitive)) {
        n.chop(territory.size());
        n = n.trimmed();
        for (const char* joiner : {" de", " do", " da", " di", " du", " von", " of"})
            if (n.endsWith(QLatin1String(joiner))) n.chop(int(qstrlen(joiner)));
    }
    n = n.trimmed();
    if (!n.isEmpty()) n[0] = n[0].toUpper();
    return n;
}

QString readerLanguage() { return g_reader; }

void setReaderLanguage(const QString& code)
{
    g_reader = readerLanguages().contains(code) ? code : QStringLiteral("en");
}

}  // namespace core::languages

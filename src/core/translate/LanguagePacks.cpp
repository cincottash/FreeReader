#include "LanguagePacks.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>

namespace core::languages {

namespace {
QString g_reader = QStringLiteral("en");

QString tr(const char* text) { return QCoreApplication::translate("LanguagePacks", text); }
}  // namespace

qint64 LanguagePack::downloadSize() const
{
    qint64 n = 0;
    for (const PackPart& p : parts) n += p.source.size;
    return n;
}

QString LanguagePack::contents() const
{
    QStringList what;
    const QString name = displayName(language);
    bool translator = false;
    for (const PackPart& p : parts) {
        if (p.kind == PackPart::Dictionary)
            what << QCoreApplication::translate("LanguagePacks", "%1 → %2 dictionary").arg(name, displayName(reader));
        else if (p.kind == PackPart::Definitions)
            what << QCoreApplication::translate("LanguagePacks", "%1 definitions").arg(name);
        else if (!translator) {
            translator = true;   // the program and its model: one item
            what << tr("the offline translator");
        }
    }
    if (what.size() <= 1) return what.join(QString());
    const QString last = what.takeLast();
    return what.join(QStringLiteral(", ")) + tr(" and ") + last;
}

QStringList readerLanguages() { return {QStringLiteral("en"), QStringLiteral("es")}; }

QList<LanguagePack> packsFor(const QString& reader)
{
    QList<LanguagePack> out;
    const DictionaryDownload::Source translator = DictionaryDownload::offlineTranslator();
    // Complete only with the translator: where no translator is built (another processor), a language read in another
    // language isn't offered as a pack (its dictionaries stay downloadable one by one in Settings > Translation).
    if (reader == QLatin1String("en")) {
        if (translator.size > 0) {
            LanguagePack es;
            es.language = QStringLiteral("es");
            es.reader = reader;
            es.parts << PackPart{PackPart::Dictionary, QStringLiteral("es"), DictionaryDownload::spanishEnglish()};
            es.parts << PackPart{PackPart::Definitions, QStringLiteral("es-es"), DictionaryDownload::spanishDefinitions()};
            es.parts << PackPart{PackPart::Translator, QStringLiteral("translator"), translator};   // with its Spanish -> English model
            out << es;
        }
        LanguagePack en;   // English books for an English reader: definitions only (nothing to translate)
        en.language = QStringLiteral("en");
        en.reader = reader;
        en.parts << PackPart{PackPart::Definitions, QStringLiteral("en"), DictionaryDownload::englishDefinitions()};
        out << en;
    } else if (reader == QLatin1String("es")) {
        const DictionaryDownload::Source model = DictionaryDownload::translatorModel(QStringLiteral("en-es"));
        if (translator.size > 0 && model.size > 0) {
            LanguagePack en;
            en.language = QStringLiteral("en");
            en.reader = reader;
            en.parts << PackPart{PackPart::Dictionary, QStringLiteral("en-es"), DictionaryDownload::englishSpanish()};
            en.parts << PackPart{PackPart::Definitions, QStringLiteral("en"), DictionaryDownload::englishDefinitions()};
            en.parts << PackPart{PackPart::Translator, QStringLiteral("translator"), translator};
            en.parts << PackPart{PackPart::TranslatorModel, QStringLiteral("model-en-es"), model};
            out << en;
        }
        LanguagePack es;   // Spanish books for a Spanish reader: definitions only
        es.language = QStringLiteral("es");
        es.reader = reader;
        es.parts << PackPart{PackPart::Definitions, QStringLiteral("es-es"), DictionaryDownload::spanishDefinitions()};
        out << es;
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

QString displayName(const QString& code)
{
    // The languages FreeReader reads, by name in the interface's language (Spanish writes them in lower case).
    static const QHash<QString, const char*> names = {
        {QStringLiteral("en"), QT_TRANSLATE_NOOP("LanguagePacks", "English")}, {QStringLiteral("es"), QT_TRANSLATE_NOOP("LanguagePacks", "Spanish")},
        {QStringLiteral("fr"), QT_TRANSLATE_NOOP("LanguagePacks", "French")},  {QStringLiteral("de"), QT_TRANSLATE_NOOP("LanguagePacks", "German")},
        {QStringLiteral("it"), QT_TRANSLATE_NOOP("LanguagePacks", "Italian")}, {QStringLiteral("pt"), QT_TRANSLATE_NOOP("LanguagePacks", "Portuguese")}};
    const QString base = code.section(QLatin1Char('-'), 0, 0).toLower();
    if (const auto it = names.constFind(base); it != names.constEnd()) return tr(it.value());
    return englishName(base);
}

QString displayTitle(const QString& code)
{
    QString n = displayName(code);
    if (!n.isEmpty()) n[0] = n[0].toUpper();
    return n;
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

QString suggestedReaderLanguage()
{
    const QString own = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
    return readerLanguages().contains(own) ? own : QStringLiteral("en");
}

}  // namespace core::languages

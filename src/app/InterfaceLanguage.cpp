#include "InterfaceLanguage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

bool JsonTranslator::loadFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    return loadJson(f.readAll(), error);
}

bool JsonTranslator::loadJson(const QByteArray& json, QString* error)
{
    QJsonParseError e;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = e.error != QJsonParseError::NoError ? e.errorString() : QStringLiteral("not a JSON object");
        return false;
    }
    m_strings.clear();
    const QJsonObject o = doc.object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (it.key().startsWith(QLatin1Char('#'))) continue;   // "#language", "#notes": about the file
        if (it.value().isString() && !it.value().toString().isEmpty()) m_strings.insert(it.key(), it.value().toString());
        else if (it.value().isObject()) m_strings.insert(it.key(), it.value().toObject().toVariantMap());
    }
    return true;
}

QString JsonTranslator::translate(const char*, const char* sourceText, const char*, int n) const
{
    if (!sourceText) return {};
    const auto it = m_strings.constFind(QString::fromUtf8(sourceText));
    if (it == m_strings.constEnd()) return {};   // empty: Qt keeps the English
    if (it->typeId() == QMetaType::QString) return it->toString();
    const QVariantMap forms = it->toMap();
    QString s = (n == 1 ? forms.value(QStringLiteral("one")) : forms.value(QStringLiteral("other"))).toString();
    if (s.isEmpty()) s = forms.value(QStringLiteral("other")).toString();
    if (n >= 0) s.replace(QStringLiteral("%n"), QLocale().toString(n));
    return s;
}

bool installInterfaceLanguage(const QString& code, const QString& dataFolder)
{
    // The one installed before goes first (English: back to the app's own text).
    static JsonTranslator* installed = nullptr;
    if (installed) {
        QCoreApplication::removeTranslator(installed);
        delete installed;
        installed = nullptr;
    }
    if (code.isEmpty() || code == QLatin1String("en")) return false;
    auto* t = new JsonTranslator;
    const QString own = dataFolder.isEmpty() ? QString() : QDir(dataFolder).filePath(QStringLiteral("languages/%1.json").arg(code));
    const bool ok = (!own.isEmpty() && QFile::exists(own) && t->loadFile(own)) || t->loadFile(QStringLiteral(":/languages/%1.json").arg(code));
    if (!ok || t->isEmpty()) {
        delete t;
        return false;
    }
    QCoreApplication::installTranslator(t);
    installed = t;
    return true;
}

// The app's own text (menus, buttons, messages) in the reader's language: a translation file per language, JSON
// {"English text": "translated text"}, or for counts {"%n words": {"one": "%n palabra", "other": "%n palabras"}}.
// Every string the app shows goes through tr(), so a language is added by adding its file; a string missing from the
// file stays in English. English needs no file.
//
// The files are part of a language's support: a reader language is offered only when its file is complete
// (core/translate/LanguagePacks.h). tests/extract_strings.py lists every string, as the template for a new file.
#pragma once

#include <QHash>
#include <QString>
#include <QTranslator>
#include <QVariant>

class JsonTranslator : public QTranslator {
public:
    bool loadFile(const QString& path, QString* error = nullptr);
    bool loadJson(const QByteArray& json, QString* error = nullptr);
    int count() const { return int(m_strings.size()); }
    bool isEmpty() const override { return m_strings.isEmpty(); }
    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override;

private:
    QHash<QString, QVariant> m_strings;   // a QString, or a QVariantMap {one, other}
};

// The file for a language: in the app's resources (":/languages/<code>.json"), or one put in the app data folder's
// "languages" folder (it wins, so a corrected file can be tried without a new build). Installs it for the whole
// app; English (or a language without a file) installs nothing. False when nothing was installed.
bool installInterfaceLanguage(const QString& code, const QString& dataFolder);

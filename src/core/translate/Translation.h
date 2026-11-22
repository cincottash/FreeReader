// Translation of selected text: what the reader asks for, what a provider answers, and the interface every
// provider (online service, local AI model, offline dictionary) implements.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace core {

struct TranslationRequest {
    QString text;       // the selection
    QString sentence;   // the sentence it appears in (context)
    QString from;       // source language code ("es"), from the book
    QString to = QStringLiteral("en");

    bool isWord() const;     // one word (no spaces), so a dictionary entry fits
    QString cacheKey() const;
};

struct TranslationResult {
    QString text;              // the selection, as asked
    QString translation;       // the English meaning, in this sentence
    QStringList alternatives;  // other meanings of a word
    QString partOfSpeech;      // "noun, masculine"
    QString lemma;             // the dictionary form when it differs ("tener" for "tenía")
    QString definition;        // a short definition in the book's language
    QString inSentence;        // the whole sentence in English
    QString note;              // one short remark (an old meaning, an idiom), optional
    QString provider;          // shown in the pop-up: "Gemini", "LM Studio", "Offline dictionary"
    bool fromCache = false;

    bool isEmpty() const { return translation.isEmpty() && definition.isEmpty(); }
    QJsonObject toJson() const;
    static TranslationResult fromJson(const QJsonObject& o);
};

// Why a provider gave no answer. The service moves on to the next provider for every one of these.
enum class TranslationFailure {
    None,
    NotSetUp,        // no key, switched off, offline-only mode
    Offline,         // no connection / server not running
    DailyLimit,      // the free allowance for today is used up
    RateLimit,       // too many requests this minute
    BadKey,          // the key was refused
    NoModel,         // LM Studio is running but has no model loaded
    NotFound,        // not in the offline dictionary
    Failed,          // anything else (bad answer, server error)
};

QString failureText(TranslationFailure f);

class TranslationProvider : public QObject {
    Q_OBJECT
public:
    using Done = std::function<void(const TranslationResult& result, TranslationFailure failure, const QString& detail)>;
    using QObject::QObject;
    virtual QString name() const = 0;
    virtual bool isSetUp() const = 0;
    // True when the selected text leaves the computer (skipped in offline-only mode).
    virtual bool sendsTextOnline() const { return false; }
    // False when this provider can't take the request at all (another language's dictionary): skipped quietly.
    virtual bool handles(const TranslationRequest&) const { return true; }
    // Calls `done` exactly once, later (never from inside this call).
    virtual void translate(const TranslationRequest& request, Done done) = 0;
};

}  // namespace core

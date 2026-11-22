// Translation by AI models: Google's Gemini API (free tier, online) and LM Studio's local server (a model on this
// computer). Both get the same question and answer with the same JSON, so one answer fills the whole pop-up:
// the meaning in this sentence, other meanings, the dictionary form, a definition in the book's language and the
// whole sentence in English.
#pragma once

#include "Translation.h"

#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace core {

namespace ai {
QString systemPrompt(const TranslationRequest& r);
QString userPrompt(const TranslationRequest& r);
QJsonObject answerSchema();
// The JSON object in a model's answer (tolerates code fences and text around it). Empty when there is none.
QJsonObject parseAnswer(const QString& text);
// Only addresses on this computer may replace the real Gemini address (tests use a local fake server).
QUrl localOverride(const char* envVar);
}  // namespace ai

class GeminiProvider : public TranslationProvider {
    Q_OBJECT
public:
    static constexpr const char* kDefaultModel = "gemini-flash-lite-latest";
    GeminiProvider(QNetworkAccessManager* net, QObject* parent = nullptr);
    QString name() const override { return QStringLiteral("Gemini"); }
    bool isSetUp() const override { return m_enabled && !m_key.isEmpty(); }
    bool sendsTextOnline() const override { return true; }
    void translate(const TranslationRequest& request, Done done) override;

    void setKey(const QString& key) { m_key = key.trimmed(); }
    bool hasKey() const { return !m_key.isEmpty(); }
    void setModel(const QString& model) { m_model = model.trimmed().isEmpty() ? QString::fromLatin1(kDefaultModel) : model.trimmed(); }
    QString model() const { return m_model; }
    void setEnabled(bool on) { m_enabled = on; }
    QUrl baseUrl() const { return m_base; }

private:
    void send(const TranslationRequest& request, Done done, bool simple);

    QNetworkAccessManager* m_net;
    QString m_key;
    QString m_model = QString::fromLatin1(kDefaultModel);
    QUrl m_base;
    bool m_enabled = true;
};

class LmStudioProvider : public TranslationProvider {
    Q_OBJECT
public:
    LmStudioProvider(QNetworkAccessManager* net, QObject* parent = nullptr);
    QString name() const override { return QStringLiteral("LM Studio"); }
    bool isSetUp() const override { return m_enabled && m_base.isValid(); }
    void translate(const TranslationRequest& request, Done done) override;

    void setAddress(const QString& address);   // "http://localhost:1234"
    QString address() const { return m_base.toString(); }
    void setEnabled(bool on) { m_enabled = on; }
    // Asks which model is loaded: `done(model, failure)`; model empty when none.
    void checkModel(std::function<void(const QString& model, TranslationFailure failure)> done);

private:
    void ask(const TranslationRequest& request, const QString& model, Done done, bool simple);

    QNetworkAccessManager* m_net;
    QUrl m_base;
    bool m_enabled = true;
};

}  // namespace core

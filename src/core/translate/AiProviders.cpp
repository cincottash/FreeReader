#include "AiProviders.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace core {

namespace {

QString languageName(const QString& code)
{
    if (code.isEmpty()) return QStringLiteral("the book's language");
    const QLocale l(code);
    if (l.language() == QLocale::C) return code;
    return QLocale::languageToString(l.language());
}

// Calls `done` on the next turn of the event loop (providers never answer from inside translate()).
void later(QObject* ctx, std::function<void()> f) { QTimer::singleShot(0, ctx, std::move(f)); }

QJsonObject readJson(QNetworkReply* reply, QByteArray* raw = nullptr)
{
    const QByteArray body = reply->readAll();
    if (raw) *raw = body;
    return QJsonDocument::fromJson(body).object();
}

bool isConnectionError(QNetworkReply::NetworkError e)
{
    switch (e) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::SslHandshakeFailedError:
        return true;
    default:
        return false;
    }
}

TranslationResult resultFrom(const QJsonObject& answer, const TranslationRequest& r, const QString& provider)
{
    TranslationResult res = TranslationResult::fromJson(answer);
    res.text = r.text.trimmed();
    res.provider = provider;
    if (!r.isWord()) {
        // A passage has no dictionary entry.
        res.alternatives.clear();
        res.partOfSpeech.clear();
        res.lemma.clear();
    }
    if (res.lemma.compare(res.text, Qt::CaseInsensitive) == 0) res.lemma.clear();
    return res;
}

}  // namespace

namespace ai {

QString systemPrompt(const TranslationRequest& r)
{
    return QStringLiteral("You are a careful bilingual dictionary and translator. You help a reader whose language is %1 "
                          "read a book written in %2. Answer only with the requested JSON object.")
        .arg(languageName(r.to), languageName(r.from));
}

QString userPrompt(const TranslationRequest& r)
{
    const QString from = languageName(r.from), to = languageName(r.to);
    QString p;
    p += QStringLiteral("Selected text: «%1»\n").arg(r.text.trimmed());
    p += QStringLiteral("Sentence it appears in: «%1»\n\n").arg(r.sentence.isEmpty() ? r.text.trimmed() : r.sentence);
    p += QStringLiteral("Fill in this JSON:\n");
    p += QStringLiteral("- translation: what the selected text means in this sentence, in %1. For one word give 1 to 4 %1 words; "
                        "for a phrase or a passage give a natural %1 translation of the selected text only.\n").arg(to);
    p += QStringLiteral("- alternatives: for a single word, up to 4 other common %1 meanings of it, not repeating translation; "
                        "otherwise an empty list.\n").arg(to);
    p += QStringLiteral("- part_of_speech: for a single word, its part of speech in %1, with gender for nouns and tense for verbs "
                        "(for example \"noun, masculine\" or \"verb, imperfect\"); otherwise \"\".\n").arg(to);
    p += QStringLiteral("- lemma: for a single word, its dictionary form in %1 when it differs from the selected text "
                        "(the infinitive of a verb, the singular of a noun); otherwise \"\".\n").arg(from);
    p += QStringLiteral("- definition: for a word or a short phrase, a short definition written in %1, as a %1 dictionary "
                        "gives it; otherwise \"\".\n").arg(from);
    p += QStringLiteral("- in_sentence: the whole sentence translated into natural %1.\n").arg(to);
    p += QStringLiteral("- note: one short %1 sentence only when it matters for understanding (an old or regional meaning, "
                        "an idiom, a pun); otherwise \"\".\n").arg(to);
    return p;
}

QJsonObject answerSchema()
{
    auto str = [] { return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}; };
    QJsonObject props{
        {QStringLiteral("translation"), str()},
        {QStringLiteral("alternatives"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), str()}}},
        {QStringLiteral("part_of_speech"), str()},
        {QStringLiteral("lemma"), str()},
        {QStringLiteral("definition"), str()},
        {QStringLiteral("in_sentence"), str()},
        {QStringLiteral("note"), str()},
    };
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), props},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("translation"), QStringLiteral("alternatives"), QStringLiteral("part_of_speech"),
                                                QStringLiteral("lemma"), QStringLiteral("definition"), QStringLiteral("in_sentence"),
                                                QStringLiteral("note")}},
        {QStringLiteral("additionalProperties"), false},
    };
}

QJsonObject parseAnswer(const QString& text)
{
    const int a = text.indexOf(QLatin1Char('{')), b = text.lastIndexOf(QLatin1Char('}'));
    if (a < 0 || b <= a) return {};
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(text.mid(a, b - a + 1).toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isObject()) return {};
    const QJsonObject o = d.object();
    return o.value(QLatin1String("translation")).toString().trimmed().isEmpty() ? QJsonObject() : o;
}

QUrl localOverride(const char* envVar)
{
    const QUrl u(qEnvironmentVariable(envVar));
    if (!u.isValid() || u.scheme() != QLatin1String("http")) return {};
    if (u.host() != QLatin1String("127.0.0.1") && u.host() != QLatin1String("localhost")) return {};
    return u;
}

}  // namespace ai

// ---------------------------------------------------------------- Gemini

GeminiProvider::GeminiProvider(QNetworkAccessManager* net, QObject* parent) : TranslationProvider(parent), m_net(net)
{
    m_base = ai::localOverride("FREEREADER_GEMINI_URL");
    if (!m_base.isValid()) m_base = QUrl(QStringLiteral("https://generativelanguage.googleapis.com"));
}

void GeminiProvider::translate(const TranslationRequest& request, Done done)
{
    if (!isSetUp()) {
        later(this, [done] { done({}, TranslationFailure::NotSetUp, {}); });
        return;
    }
    send(request, std::move(done), false);
}

void GeminiProvider::send(const TranslationRequest& request, Done done, bool simple)
{
    QUrl url = m_base;
    url.setPath(QStringLiteral("/v1beta/models/%1:generateContent").arg(m_model));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("x-goog-api-key", m_key.toUtf8());
    req.setTransferTimeout(25000);

    QJsonObject gen{{QStringLiteral("responseMimeType"), QStringLiteral("application/json")}, {QStringLiteral("maxOutputTokens"), 2048}};
    if (!simple) {
        // A strict answer shape, and as little "thinking" as the model allows (faster, and it doesn't use up tokens).
        gen[QLatin1String("responseJsonSchema")] = ai::answerSchema();
        gen[QLatin1String("thinkingConfig")] = QJsonObject{{QStringLiteral("thinkingLevel"), QStringLiteral("minimal")}};
    }
    const QJsonObject body{
        {QStringLiteral("systemInstruction"), QJsonObject{{QStringLiteral("parts"), QJsonArray{QJsonObject{{QStringLiteral("text"), ai::systemPrompt(request)}}}}}},
        {QStringLiteral("contents"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                            {QStringLiteral("parts"), QJsonArray{QJsonObject{{QStringLiteral("text"), ai::userPrompt(request)}}}}}}},
        {QStringLiteral("generationConfig"), gen},
    };
    QNetworkReply* reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, request, done, simple] {
        reply->deleteLater();
        QByteArray raw;
        const QJsonObject o = readJson(reply, &raw);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString message = o.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString();
        if (status == 0 && isConnectionError(reply->error())) {
            done({}, TranslationFailure::Offline, reply->errorString());
            return;
        }
        if (status == 429) {
            // Per day ("...PerDay...") means the free allowance is gone until midnight Pacific time; otherwise it's
            // the per-minute limit.
            done({}, raw.contains("PerDay") ? TranslationFailure::DailyLimit : TranslationFailure::RateLimit, message);
            return;
        }
        if (status == 401 || status == 403 || raw.contains("API_KEY_INVALID") || raw.contains("API key not valid")) {
            done({}, TranslationFailure::BadKey, message);
            return;
        }
        if (status == 400 && !simple) {
            // An option this model doesn't take (the answer shape or thinking level): ask again without them.
            send(request, done, true);
            return;
        }
        if (status != 200) {
            done({}, TranslationFailure::Failed, message.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : message);
            return;
        }
        QString text;
        const QJsonArray parts = o.value(QLatin1String("candidates")).toArray().first().toObject()
                                     .value(QLatin1String("content")).toObject().value(QLatin1String("parts")).toArray();
        for (const QJsonValue& p : parts)
            if (!p.toObject().value(QLatin1String("thought")).toBool()) text += p.toObject().value(QLatin1String("text")).toString();
        const QJsonObject answer = ai::parseAnswer(text);
        if (answer.isEmpty()) {
            done({}, TranslationFailure::Failed, QStringLiteral("unreadable answer"));
            return;
        }
        done(resultFrom(answer, request, name()), TranslationFailure::None, {});
    });
}

// ---------------------------------------------------------------- LM Studio

LmStudioProvider::LmStudioProvider(QNetworkAccessManager* net, QObject* parent) : TranslationProvider(parent), m_net(net)
{
    setAddress(QStringLiteral("http://localhost:1234"));
}

void LmStudioProvider::setAddress(const QString& address)
{
    QString a = address.trimmed();
    if (a.isEmpty()) a = QStringLiteral("http://localhost:1234");
    if (!a.contains(QLatin1String("://"))) a.prepend(QLatin1String("http://"));
    while (a.endsWith(QLatin1Char('/'))) a.chop(1);
    if (a.endsWith(QLatin1String("/v1"))) a.chop(3);
    m_base = QUrl(a);
}

void LmStudioProvider::listModels(std::function<void(const QList<Model>&, TranslationFailure)> done)
{
    // LM Studio's own list has every downloaded model and says which are loaded; the OpenAI-style list (older
    // versions) has only what can answer now, so there every model counts as loaded.
    QUrl url = m_base;
    url.setPath(QStringLiteral("/api/v0/models"));
    QNetworkRequest req(url);
    req.setTransferTimeout(2500);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0) {
            done({}, TranslationFailure::Offline);
            return;
        }
        const QJsonObject o = readJson(reply);
        if (status == 200 && o.value(QLatin1String("data")).isArray()) {
            QList<Model> out;
            for (const QJsonValue& v : o.value(QLatin1String("data")).toArray()) {
                const QJsonObject m = v.toObject();
                const QString type = m.value(QLatin1String("type")).toString();
                if (type != QLatin1String("llm") && type != QLatin1String("vlm")) continue;
                out << Model{m.value(QLatin1String("id")).toString(), m.value(QLatin1String("state")).toString() == QLatin1String("loaded")};
            }
            done(out, TranslationFailure::None);
            return;
        }
        QUrl url2 = m_base;
        url2.setPath(QStringLiteral("/v1/models"));
        QNetworkRequest req2(url2);
        req2.setTransferTimeout(2500);
        QNetworkReply* r2 = m_net->get(req2);
        connect(r2, &QNetworkReply::finished, this, [r2, done] {
            r2->deleteLater();
            if (r2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
                done({}, TranslationFailure::Offline);
                return;
            }
            QList<Model> out;
            for (const QJsonValue& v : readJson(r2).value(QLatin1String("data")).toArray()) {
                const QString id = v.toObject().value(QLatin1String("id")).toString();
                if (!id.isEmpty() && !id.contains(QLatin1String("embed"), Qt::CaseInsensitive)) out << Model{id, true};
            }
            done(out, TranslationFailure::None);
        });
    });
}

void LmStudioProvider::checkModel(std::function<void(const QString&, TranslationFailure)> done)
{
    const QString chosen = m_model;
    listModels([chosen, done](const QList<Model>& models, TranslationFailure f) {
        if (f != TranslationFailure::None) {
            done({}, f);
            return;
        }
        // The chosen model when LM Studio has it (it loads it if needed), otherwise the loaded one.
        if (!chosen.isEmpty())
            for (const Model& m : models)
                if (m.id == chosen) {
                    done(m.id, TranslationFailure::None);
                    return;
                }
        for (const Model& m : models)
            if (m.loaded) {
                done(m.id, TranslationFailure::None);
                return;
            }
        done({}, TranslationFailure::NoModel);
    });
}

void LmStudioProvider::translate(const TranslationRequest& request, Done done)
{
    if (!isSetUp()) {
        later(this, [done] { done({}, TranslationFailure::NotSetUp, {}); });
        return;
    }
    QPointer<LmStudioProvider> self(this);
    checkModel([self, request, done](const QString& model, TranslationFailure f) {
        if (!self) return;
        if (f != TranslationFailure::None) {
            done({}, f, {});
            return;
        }
        self->ask(request, model, done, false);
    });
}

void LmStudioProvider::ask(const TranslationRequest& request, const QString& model, Done done, bool simple)
{
    QUrl url = m_base;
    url.setPath(QStringLiteral("/v1/chat/completions"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(120000);   // a large local model can take a while for a long passage
    QJsonObject body{
        {QStringLiteral("model"), model},
        {QStringLiteral("messages"), QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), ai::systemPrompt(request)}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), ai::userPrompt(request)}}}},
        {QStringLiteral("temperature"), 0.2},
        {QStringLiteral("max_tokens"), 3000},
        {QStringLiteral("stream"), false},
    };
    if (!simple)
        body[QLatin1String("response_format")] = QJsonObject{
            {QStringLiteral("type"), QStringLiteral("json_schema")},
            {QStringLiteral("json_schema"), QJsonObject{{QStringLiteral("name"), QStringLiteral("translation")},
                                                        {QStringLiteral("strict"), true},
                                                        {QStringLiteral("schema"), ai::answerSchema()}}}};
    QNetworkReply* reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, request, model, done, simple] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = readJson(reply);
        if (status == 0) {
            done({}, TranslationFailure::Offline, reply->errorString());
            return;
        }
        if (status == 400 && !simple) {
            ask(request, model, done, true);
            return;
        }
        if (status != 200) {
            done({}, TranslationFailure::Failed, QStringLiteral("HTTP %1").arg(status));
            return;
        }
        const QString text = o.value(QLatin1String("choices")).toArray().first().toObject()
                                 .value(QLatin1String("message")).toObject().value(QLatin1String("content")).toString();
        const QJsonObject answer = ai::parseAnswer(text);
        if (answer.isEmpty()) {
            done({}, TranslationFailure::Failed, QStringLiteral("unreadable answer"));
            return;
        }
        done(resultFrom(answer, request, name()), TranslationFailure::None, {});
    });
}

}  // namespace core

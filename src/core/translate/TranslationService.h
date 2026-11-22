// Every translation goes through here: the cache first, then the providers in order (Gemini, LM Studio, the
// offline ones), moving on when one can't answer. A provider whose free allowance is used up is skipped until
// the allowance renews; one that is rate-limited, for a minute.
#pragma once

#include "Translation.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>

#include <vector>

namespace core {

class Library;

class TranslationService : public QObject {
    Q_OBJECT
public:
    explicit TranslationService(QObject* parent = nullptr);

    void setCache(Library* library) { m_cache = library; }
    void setProviders(const std::vector<TranslationProvider*>& inOrder) { m_providers = inOrder; }
    // Offline only: no provider that sends text over the internet is asked (cached answers still show).
    void setOfflineOnly(bool on) { m_offlineOnly = on; }
    bool offlineOnly() const { return m_offlineOnly; }

    // Starts a lookup; `finished` reports it with the returned id. A newer lookup doesn't cancel older ones.
    int translate(const TranslationRequest& request);

    // What happened to each provider on the last lookup that reached it ("Gemini: today's free limit is used up").
    QString lastStatus(const QString& provider) const { return m_status.value(provider); }
    // When a provider is skipped because of its limit; invalid when it isn't.
    QDateTime pausedUntil(const QString& provider) const;
    void clearPauses() { m_pausedUntil.clear(); }

signals:
    void asking(int id, const QString& provider);
    // `result.isEmpty()` when nobody could answer; `why` then says what each provider reported.
    void finished(int id, const core::TranslationResult& result, const QString& why);

private:
    void next(int id, const TranslationRequest& request, size_t from, QStringList why);

    Library* m_cache = nullptr;
    std::vector<TranslationProvider*> m_providers;
    bool m_offlineOnly = false;
    int m_nextId = 1;
    QHash<QString, QString> m_status;
    QHash<QString, QDateTime> m_pausedUntil;
};

// The next midnight in Pacific time (when Gemini's daily allowance renews), in UTC.
QDateTime nextPacificMidnight(const QDateTime& nowUtc);

}  // namespace core

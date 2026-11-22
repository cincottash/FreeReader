#include "TranslationService.h"

#include "core/services/Library.h"

#include <QTimeZone>
#include <QTimer>

namespace core {

QDateTime nextPacificMidnight(const QDateTime& nowUtc)
{
    const QTimeZone pacific("America/Los_Angeles");
    if (!pacific.isValid()) return nowUtc.addSecs(24 * 3600);
    const QDateTime local = nowUtc.toTimeZone(pacific);
    return QDateTime(local.date().addDays(1), QTime(0, 0), pacific).toUTC();
}

TranslationService::TranslationService(QObject* parent) : QObject(parent) {}

QDateTime TranslationService::pausedUntil(const QString& provider) const
{
    const QDateTime t = m_pausedUntil.value(provider);
    return t.isValid() && t > QDateTime::currentDateTimeUtc() ? t : QDateTime();
}

int TranslationService::translate(const TranslationRequest& request)
{
    const int id = m_nextId++;
    if (m_cache) {
        const QJsonObject cached = m_cache->cachedTranslation(request.cacheKey());
        if (!cached.isEmpty()) {
            TranslationResult r = TranslationResult::fromJson(cached);
            r.fromCache = true;
            QTimer::singleShot(0, this, [this, id, r] { emit finished(id, r, {}); });
            return id;
        }
    }
    const QString key = request.cacheKey();
    if (m_inFlight.contains(key)) {
        m_inFlight[key] << id;
        return id;
    }
    m_inFlight.insert(key, {id});
    QTimer::singleShot(0, this, [this, id, request] { next(id, request, 0, {}); });
    return id;
}

void TranslationService::next(int id, const TranslationRequest& request, size_t from, QStringList why)
{
    for (size_t i = from; i < m_providers.size(); ++i) {
        TranslationProvider* p = m_providers[i];
        if (!p) continue;
        if (!p->isSetUp()) continue;
        if (m_offlineOnly && p->sendsTextOnline()) continue;
        if (const QDateTime until = pausedUntil(p->name()); until.isValid()) {
            why << QStringLiteral("%1: %2").arg(p->name(), m_status.value(p->name()));
            continue;
        }
        emit asking(id, p->name());
        QPointer<TranslationService> self(this);
        const QString name = p->name();
        p->translate(request, [self, id, request, i, why, name](const TranslationResult& r, TranslationFailure f, const QString& detail) mutable {
            if (!self) return;
            if (f == TranslationFailure::None && !r.isEmpty()) {
                self->m_status.remove(name);
                if (self->m_cache) self->m_cache->cacheTranslation(request.cacheKey(), name, r.toJson());
                self->finish(request, r, {});
                return;
            }
            const QString text = failureText(f == TranslationFailure::None ? TranslationFailure::Failed : f);
            self->m_status.insert(name, text);
            if (f == TranslationFailure::DailyLimit)
                self->m_pausedUntil.insert(name, nextPacificMidnight(QDateTime::currentDateTimeUtc()));
            else if (f == TranslationFailure::RateLimit)
                self->m_pausedUntil.insert(name, QDateTime::currentDateTimeUtc().addSecs(60));
            why << QStringLiteral("%1: %2").arg(name, text);
            Q_UNUSED(detail);
            self->next(id, request, i + 1, why);
        });
        return;
    }
    Q_UNUSED(id);
    finish(request, {}, why.join(QLatin1Char('\n')));
}

void TranslationService::finish(const TranslationRequest& request, const TranslationResult& r, const QString& why)
{
    const QList<int> ids = m_inFlight.take(request.cacheKey());
    for (int id : ids) emit finished(id, r, why);
}

}  // namespace core

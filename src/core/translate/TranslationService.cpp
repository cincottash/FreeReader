#include "TranslationService.h"

#include "core/services/Library.h"
#include "OfflineDictionary.h"

#include <QLocale>
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
        QString from;
        const QJsonObject cached = m_cache->cachedTranslation(request.cacheKey(), &from);
        if (!cached.isEmpty() && cacheFits(from, request)) {
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
    // One answer only: the first provider that can answer gives it; the next is asked only when one fails or an
    // online one takes too long (no quick offline answer that Gemini's then replaces: the owner's requests,
    // 2026-11-23 and 2026-11-26).
    auto state = std::make_shared<Lookup>();
    QTimer::singleShot(0, this, [this, id, request, state] { next(id, request, 0, {}, state); });
    return id;
}

bool TranslationService::cacheFits(const QString& provider, const TranslationRequest& request) const
{
    // The order decides, never what was kept: a kept answer is used only when no engine before its own in the list
    // would be asked now, and its own engine is still on (Gemini, then LM Studio, then the offline ones: the
    // owner's rule, 2026-11-23). One that is only held back (offline only, or paused for its limit) still counts:
    // its kept answers work with no internet.
    for (TranslationProvider* p : m_providers) {
        if (!p) continue;
        if (p->name() == provider) return p->isSetUp() && p->handles(request);
        const bool asked = p->isSetUp() && p->handles(request) && !(m_offlineOnly && p->sendsTextOnline()) &&
                           !pausedUntil(p->name()).isValid();
        if (asked) return false;
    }
    return false;   // an engine that isn't in the list any more
}

TranslationResult TranslationService::offlineMeaning(const QString& text, const QString& from) const
{
    TranslationRequest r;
    r.text = text;
    r.from = from;
    for (OfflineDictionary* d : m_instant)
        if (d && d->isSetUp() && d->handles(r)) {
            const TranslationResult res = d->lookUp(text, from);
            if (!res.translation.isEmpty()) return res;
        }
    return {};
}

bool TranslationService::hasOfflineDictionary(const QString& language) const
{
    for (OfflineDictionary* d : m_instant)
        if (d && d->info().ok && (language.isEmpty() || d->info().language == language) &&
            (d->info().target.isEmpty() || d->info().target == languages::readerLanguage()))
            return true;
    return false;
}

void TranslationService::recordFailure(const QString& name, TranslationFailure f)
{
    m_status.insert(name, failureText(f));
    if (f == TranslationFailure::DailyLimit)
        m_pausedUntil.insert(name, nextPacificMidnight(QDateTime::currentDateTimeUtc()));
    else if (f == TranslationFailure::RateLimit)
        m_pausedUntil.insert(name, QDateTime::currentDateTimeUtc().addSecs(60));
}

void TranslationService::next(int id, const TranslationRequest& request, size_t from, QStringList why, std::shared_ptr<Lookup> state)
{
    for (size_t i = from; i < m_providers.size(); ++i) {
        TranslationProvider* p = m_providers[i];
        if (!p) continue;
        if (!p->isSetUp() || !p->handles(request)) continue;
        if (m_offlineOnly && p->sendsTextOnline()) continue;
        if (const QDateTime until = pausedUntil(p->name()); until.isValid()) {
            why << QStringLiteral("%1: %2").arg(p->name(), m_status.value(p->name()));
            continue;
        }
        emit asking(id, p->name());
        QPointer<TranslationService> self(this);
        const QString name = p->name();
        const bool keep = p->cacheable();
        const bool reportsMisses = p->reportsMisses();
        // Answered (or failed) in time; or given up on, and the next provider asked meanwhile.
        auto answered = std::make_shared<bool>(false);
        auto gaveUp = std::make_shared<bool>(false);
        if (p->sendsTextOnline() && request.kind == TranslationRequest::Kind::Lookup && m_onlineWaitMs > 0) {
            QTimer::singleShot(m_onlineWaitMs, this, [this, id, request, i, why, name, state, answered, gaveUp]() mutable {
                if (*answered || state->done) return;
                *gaveUp = true;
                ++state->late;
                why << QStringLiteral("%1: %2").arg(name, tr("no answer within %1 seconds").arg(QLocale().toString(m_onlineWaitMs / 1000.0, 'g', 2)));
                next(id, request, i + 1, why, state);
            });
        }
        p->translate(request, [self, id, request, i, why, name, keep, reportsMisses, state, answered, gaveUp](const TranslationResult& r, TranslationFailure f, const QString& detail) mutable {
            if (!self) return;
            *answered = true;
            Q_UNUSED(detail);
            const bool ok = f == TranslationFailure::None && !r.isEmpty();
            if (ok) {
                self->m_status.remove(name);
                if (self->m_cache && keep) self->m_cache->cacheTranslation(request.cacheKey(), name, r.toJson());
            } else if (!(f == TranslationFailure::NotFound && !reportsMisses)) {
                self->recordFailure(name, f == TranslationFailure::None ? TranslationFailure::Failed : f);
            }
            if (*gaveUp) {
                // Too late: another provider is answering or answered. Kept for next time (above), never shown over
                // that answer; shown only when nothing after it could answer.
                --state->late;
                if (state->done || !state->exhausted) return;
                if (ok) {
                    TranslationResult out = r;
                    out.keep = keep;
                    state->done = true;
                    self->finish(request, out, {});
                } else if (state->late == 0) {
                    state->done = true;
                    self->finish(request, {}, state->exhaustedWhy);
                }
                return;
            }
            if (ok) {
                TranslationResult out = r;
                out.keep = keep;
                state->done = true;
                self->finish(request, out, {});
                return;
            }
            if (!(f == TranslationFailure::NotFound && !reportsMisses))
                why << QStringLiteral("%1: %2").arg(name, failureText(f == TranslationFailure::None ? TranslationFailure::Failed : f));
            self->next(id, request, i + 1, why, state);
        });
        return;
    }
    Q_UNUSED(id);
    if (state->late > 0) {
        // Nothing after the online provider given up on could answer: its answer, when it comes, after all.
        state->exhausted = true;
        state->exhaustedWhy = why.join(QLatin1Char('\n'));
        return;
    }
    state->done = true;
    finish(request, {}, why.join(QLatin1Char('\n')));
}

void TranslationService::finish(const TranslationRequest& request, const TranslationResult& r, const QString& why)
{
    const QList<int> ids = m_inFlight.take(request.cacheKey());
    for (int id : ids) emit finished(id, r, why);
}

}  // namespace core

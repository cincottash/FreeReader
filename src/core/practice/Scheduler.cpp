#include "Scheduler.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace core::practice {

namespace {

constexpr qint64 kMinute = 60;
// Learning steps for new cards and relearning steps for forgotten ones, in seconds.
constexpr qint64 kLearningSteps[] = {1 * kMinute, 10 * kMinute};
constexpr qint64 kRelearningSteps[] = {10 * kMinute};

double clampD(double d) { return std::clamp(d, 1.0, 10.0); }

}  // namespace

const Scheduler::Weights& Scheduler::defaultWeights()
{
    // FSRS-6 default parameters (open-spaced-repetition, the awesome-fsrs wiki and py-fsrs).
    static const Weights w = {0.212, 1.2931, 2.3065, 8.2956, 6.4133, 0.8334, 3.0194, 0.001, 1.8722, 0.1666, 0.796,
                              1.4835, 0.0614, 0.2629, 1.6483, 0.6014, 1.8729, 0.5425, 0.0912, 0.0658, 0.1542};
    return w;
}

Scheduler::Scheduler(double desiredRetention, int maximumIntervalDays)
    : m_w(defaultWeights()), m_retention(std::clamp(desiredRetention, 0.7, 0.99)), m_maxInterval(std::max(1, maximumIntervalDays))
{
}

double Scheduler::factor() const { return std::pow(0.9, 1.0 / decay()) - 1.0; }

double Scheduler::retrievability(double elapsedDays, double stability) const
{
    if (stability <= 0) return 0;
    return std::pow(1.0 + factor() * std::max(0.0, elapsedDays) / stability, decay());
}

int Scheduler::intervalDays(double stability) const
{
    const double i = stability / factor() * (std::pow(m_retention, 1.0 / decay()) - 1.0);
    return std::clamp(int(std::lround(i)), 1, m_maxInterval);
}

double Scheduler::initStability(Rating r) const { return std::max(m_w[size_t(int(r) - 1)], 0.1); }

double Scheduler::initDifficulty(Rating r) const { return clampD(m_w[4] - std::exp(m_w[5] * (int(r) - 1)) + 1.0); }

double Scheduler::nextDifficulty(double d, Rating r) const
{
    const double delta = -m_w[6] * (int(r) - 3);
    const double damped = d + delta * (10.0 - d) / 9.0;   // linear damping: harder to move near the ends
    // Mean reversion towards the difficulty of an "Easy" first answer.
    return clampD(m_w[7] * initDifficulty(Rating::Easy) + (1.0 - m_w[7]) * damped);
}

double Scheduler::recallStability(double d, double s, double r, Rating rating) const
{
    const double hard = rating == Rating::Hard ? m_w[15] : 1.0;
    const double easy = rating == Rating::Easy ? m_w[16] : 1.0;
    return s * (std::exp(m_w[8]) * (11.0 - d) * std::pow(s, -m_w[9]) * (std::exp(m_w[10] * (1.0 - r)) - 1.0) * hard * easy + 1.0);
}

double Scheduler::forgetStability(double d, double s, double r) const
{
    const double f = m_w[11] * std::pow(d, -m_w[12]) * (std::pow(s + 1.0, m_w[13]) - 1.0) * std::exp(m_w[14] * (1.0 - r));
    // Never more stable than before, nor than a same-day "Again" would leave it (py-fsrs).
    return std::min(f, s / std::exp(m_w[17] * m_w[18]));
}

double Scheduler::shortTermStability(double s, Rating r) const
{
    double inc = std::exp(m_w[17] * (int(r) - 3 + m_w[18])) * std::pow(s, -m_w[19]);
    if (int(r) >= 2) inc = std::max(inc, 1.0);   // a pass never lowers stability
    return s * inc;
}

CardMemory Scheduler::review(const CardMemory& card, Rating rating, const QDateTime& now) const
{
    CardMemory c = card;
    // Whole days, as FSRS counts them.
    const double elapsed = c.lastReview.isValid() ? std::floor(std::max<qint64>(0, c.lastReview.secsTo(now)) / 86400.0) : 0.0;
    auto graduate = [&] {
        c.state = CardState::Review;
        c.step = 0;
        c.due = now.addDays(intervalDays(c.stability));
    };
    auto stepDue = [&](qint64 seconds) { c.due = now.addSecs(seconds); };

    switch (card.state) {
    case CardState::New:
        c.stability = initStability(rating);
        c.difficulty = initDifficulty(rating);
        if (rating == Rating::Easy) {
            graduate();
        } else {
            c.state = CardState::Learning;
            c.step = rating == Rating::Good ? 1 : 0;
            // Again: 1 min; Hard: halfway between the steps; Good: 10 min.
            stepDue(rating == Rating::Again ? kLearningSteps[0]
                    : rating == Rating::Hard ? (kLearningSteps[0] + kLearningSteps[1]) / 2
                                              : kLearningSteps[1]);
        }
        break;
    case CardState::Learning:
    case CardState::Relearning: {
        // Same-day answers adjust stability with the short-term formula.
        c.stability = std::max(0.01, shortTermStability(c.stability, rating));
        c.difficulty = nextDifficulty(c.difficulty, rating);
        const bool relearning = card.state == CardState::Relearning;
        const int steps = relearning ? int(std::size(kRelearningSteps)) : int(std::size(kLearningSteps));
        const qint64* table = relearning ? kRelearningSteps : kLearningSteps;
        if (rating == Rating::Again) {
            c.step = 0;
            stepDue(table[0]);
        } else if (rating == Rating::Hard) {
            stepDue(table[std::min(c.step, steps - 1)] * 3 / 2);
        } else if (rating == Rating::Easy || c.step + 1 >= steps) {
            graduate();
        } else {
            ++c.step;
            stepDue(table[c.step]);
        }
        break;
    }
    case CardState::Review: {
        const double r = retrievability(elapsed, c.stability);
        c.difficulty = nextDifficulty(c.difficulty, rating);
        if (rating == Rating::Again) {
            ++c.lapses;
            c.stability = std::max(0.001, elapsed < 1 ? shortTermStability(c.stability, rating) : forgetStability(card.difficulty, c.stability, r));
            c.state = CardState::Relearning;
            c.step = 0;
            stepDue(kRelearningSteps[0]);
        } else {
            c.stability = elapsed < 1 ? shortTermStability(c.stability, rating) : recallStability(card.difficulty, c.stability, r, rating);
            graduate();
            // Hard never beats Good, Good never beats Easy: keep the buttons in order.
            if (rating != Rating::Easy) {
                const CardMemory easy = review(card, Rating::Easy, now);
                if (c.due > easy.due) c.due = easy.due;
            }
            if (rating == Rating::Hard) {
                const CardMemory good = review(card, Rating::Good, now);
                if (c.due > good.due) c.due = good.due;
            }
        }
        break;
    }
    }
    ++c.reps;
    c.lastReview = now;
    return c;
}

QString Scheduler::describeInterval(const QDateTime& from, const QDateTime& to)
{
    const qint64 s = std::max<qint64>(0, from.secsTo(to));
    auto tr = [](const char* t) { return QCoreApplication::translate("Practice", t); };
    if (s < 3600) return tr("%1 min").arg(std::max<qint64>(1, (s + 30) / 60));
    if (s < 86400) return tr("%1 h").arg((s + 1800) / 3600);
    const double days = double(s) / 86400.0;
    if (days < 31) return tr("%1 d").arg(std::lround(days));
    if (days < 365) return tr("%1 mo").arg(QString::number(days / 30.4, 'f', days < 300 ? 1 : 0));
    return tr("%1 y").arg(QString::number(days / 365.25, 'f', 1));
}

}  // namespace core::practice

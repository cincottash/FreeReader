// Spaced repetition for practice cards: FSRS (Free Spaced Repetition Scheduler, the algorithm Anki uses since
// 23.10), version 6, with its published default parameters. Each card has a stability (days until the chance
// of remembering falls to 90 %) and a difficulty (1..10); every answer updates both and sets the next review.
// New and forgotten cards first go through short learning steps (1 and 10 minutes) on the same day.
#pragma once

#include <QDateTime>

#include <array>

namespace core::practice {

enum class Rating { Again = 1, Hard = 2, Good = 3, Easy = 4 };
enum class CardState { New = 0, Learning = 1, Review = 2, Relearning = 3 };

struct CardMemory {
    CardState state = CardState::New;
    int step = 0;               // learning step index
    QDateTime due;              // UTC
    double stability = 0;       // days
    double difficulty = 0;      // 1..10
    int reps = 0;
    int lapses = 0;
    QDateTime lastReview;       // UTC; invalid when never reviewed
};

class Scheduler {
public:
    using Weights = std::array<double, 21>;
    static const Weights& defaultWeights();

    explicit Scheduler(double desiredRetention = 0.9, int maximumIntervalDays = 36500);

    // The card after answering it with `rating` at `now`.
    CardMemory review(const CardMemory& card, Rating rating, const QDateTime& now) const;
    // When the card would be due again for each rating (for the buttons: "10 min", "3 d").
    QDateTime preview(const CardMemory& card, Rating rating, const QDateTime& now) const { return review(card, rating, now).due; }

    // The chance of remembering a card `elapsedDays` after its last review.
    double retrievability(double elapsedDays, double stability) const;
    double decay() const { return -m_w[20]; }
    double factor() const;   // so that R(S, S) = 0.9
    int intervalDays(double stability) const;

    static QString describeInterval(const QDateTime& from, const QDateTime& to);   // "1 min", "10 min", "3 d", "2 mo"

private:
    double initStability(Rating r) const;
    double initDifficulty(Rating r) const;
    double nextDifficulty(double d, Rating r) const;
    double recallStability(double d, double s, double r, Rating rating) const;
    double forgetStability(double d, double s, double r) const;
    double shortTermStability(double s, Rating r) const;

    Weights m_w;
    double m_retention;
    int m_maxInterval;
};

}  // namespace core::practice

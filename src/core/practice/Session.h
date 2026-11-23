// A practice session, shared by the desktop and the touch app: the queue of cards, what each card's front and back
// say for its direction (Spanish -> English, English -> Spanish, definition -> word, the sentence with a gap), the
// answers a typed reply is checked against, and what an answer does (saved; a card due again within 20 minutes
// comes back later in the session; one forgotten 8 times is set aside).
#pragma once

#include "Scheduler.h"

#include "core/services/WordLog.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

namespace core::practice {

struct CardFace {
    QString front;              // the word, its English, its definition, or the sentence with a gap
    QString frontDetail;        // "from tener · verb"
    QString frontSentenceHtml;  // the sentence on the front (when asked for)
    bool frontIsLong = false;   // a sentence or a long passage: smaller type
    QString back;               // the answer
    QStringList backDetail;     // other meanings, the definition, the form read
    QString backSentenceHtml;   // the sentence with the word in bold, and its English
    QString source;             // "Don Quijote · p. 12"
    bool needsMeaning = false;  // a highlighted word no dictionary knew: nothing to show yet
    bool typed = false;         // the answer is typed (the option is on and the card allows it)
};

QString withBold(const QString& sentence, const QString& word);   // rich text, the word in bold
QString withGap(const QString& sentence, const QString& word);    // the word replaced by a gap

// `sentenceFront`: the sentence on the front too; `typedOption`: answers are typed; `muted`: the color of the
// sentence's English on the back.
CardFace cardFace(const WordEntry& word, const QString& direction, bool sentenceFront, bool typedOption, const QColor& muted);
QStringList acceptedAnswers(const WordEntry& word, const QString& direction);
QString answerLanguage(const WordEntry& word, const QString& direction);   // the language a typed answer is in

// Saves the answer, sets a card forgotten 8 times aside, and puts a card due again within 20 minutes back into
// `queue` a few cards later. Returns the card as it is now.
PracticeCard answerCard(WordLog& log, const Scheduler& scheduler, QList<PracticeCard>& queue, const PracticeCard& card,
                        Rating rating, const QDateTime& now);

class Session {
public:
    explicit Session(WordLog* log) : m_log(log) {}
    // Every card the filter allows, in `order` (see WordLog::allCards).
    bool start(const WordLog::Filter& filter, const QString& order, const QDateTime& now);
    bool next();   // the next card (words deleted meanwhile are skipped); false at the end
    void rate(Rating rating, const QDateTime& now);
    void reloadWord();   // after its meaning was looked up
    void clear();

    bool isActive() const { return m_card.id != 0; }
    const PracticeCard& card() const { return m_card; }
    const WordEntry& word() const { return m_word; }
    const Scheduler& scheduler() const { return m_scheduler; }
    int done() const { return m_done; }
    int remaining() const { return int(m_queue.size()); }
    int ratings(Rating r) const { return m_ratings[int(r)]; }

private:
    WordLog* m_log;
    Scheduler m_scheduler;
    QList<PracticeCard> m_queue;
    PracticeCard m_card;
    WordEntry m_word;
    int m_done = 0;
    int m_ratings[5] = {0, 0, 0, 0, 0};
};

}  // namespace core::practice

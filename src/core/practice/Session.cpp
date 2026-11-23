#include "Session.h"

#include <QObject>

#include <algorithm>

namespace core::practice {

QString withBold(const QString& sentence, const QString& word)
{
    const int at = word.isEmpty() ? -1 : int(sentence.indexOf(word, 0, Qt::CaseInsensitive));
    if (at < 0) return sentence.toHtmlEscaped();
    return sentence.left(at).toHtmlEscaped() + QStringLiteral("<b>") + sentence.mid(at, word.size()).toHtmlEscaped() + QStringLiteral("</b>") +
           sentence.mid(at + int(word.size())).toHtmlEscaped();
}

QString withGap(const QString& sentence, const QString& word)
{
    const int at = int(sentence.indexOf(word, 0, Qt::CaseInsensitive));
    if (at < 0) return sentence;
    return sentence.left(at) + QStringLiteral("_____") + sentence.mid(at + int(word.size()));
}

CardFace cardFace(const WordEntry& w, const QString& d, bool sentenceFrontOption, bool typedOption, const QColor& muted)
{
    CardFace f;
    const bool sentenceFront = sentenceFrontOption && !w.sentence.isEmpty() && !w.passage;
    // Front
    if (d == QLatin1String("recall")) {
        f.front = w.translation;
        f.frontDetail = w.partOfSpeech;
        if (sentenceFront && !w.sentenceTranslation.isEmpty()) f.frontSentenceHtml = w.sentenceTranslation.toHtmlEscaped();
    } else if (d == QLatin1String("definition")) {
        f.front = w.definition;
        f.frontDetail = w.partOfSpeech;
    } else if (d == QLatin1String("cloze")) {
        f.front = withGap(w.sentence, w.text);
        f.frontDetail = QObject::tr("Meaning: %1").arg(w.translation);
    } else {
        f.front = w.text;
        QStringList parts;
        if (!w.lemma.isEmpty() && w.lemma.compare(w.text, Qt::CaseInsensitive) != 0) parts << QObject::tr("from %1").arg(w.lemma);
        if (!w.partOfSpeech.isEmpty()) parts << w.partOfSpeech;
        f.frontDetail = parts.join(QStringLiteral(" · "));
        if (sentenceFront) f.frontSentenceHtml = withBold(w.sentence, w.text);
    }
    f.frontIsLong = d == QLatin1String("cloze") || f.front.size() > 60;
    // Typing a long passage's translation isn't practice: those cards are always flipped. A word with no meaning
    // yet has nothing to check a typed answer against: flipped too.
    f.typed = typedOption && !w.passage && !w.translation.isEmpty();

    // Back
    if (d == QLatin1String("recognize") && w.translation.isEmpty()) {
        // A highlighted word that no dictionary knew.
        f.needsMeaning = true;
        f.back = QObject::tr("No meaning saved yet.");
        f.backDetail << QObject::tr("Look it up, or type a meaning in the Word List.");
    } else if (d == QLatin1String("recognize")) {
        f.back = w.translation;
        if (!w.alternatives.isEmpty()) f.backDetail << QObject::tr("Also: %1").arg(w.alternatives.join(QStringLiteral(", ")));
        if (!w.definition.isEmpty()) f.backDetail << w.definition;
    } else {
        f.back = d == QLatin1String("cloze") ? w.text : w.headword();
        if (!w.lemma.isEmpty() && w.lemma.compare(w.text, Qt::CaseInsensitive) != 0)
            f.backDetail << (d == QLatin1String("cloze") ? QObject::tr("from %1").arg(w.lemma) : QObject::tr("As read: %1").arg(w.text));
        if (!w.partOfSpeech.isEmpty()) f.backDetail << w.partOfSpeech;
        if (d != QLatin1String("recall")) f.backDetail << w.translation;
        else if (!w.definition.isEmpty()) f.backDetail << w.definition;
    }
    if (!w.sentence.isEmpty() && !w.passage) {
        // What the front already showed isn't repeated.
        const bool frontHasSpanish = !f.frontSentenceHtml.isEmpty() && d == QLatin1String("recognize");
        const bool frontHasEnglish = !f.frontSentenceHtml.isEmpty() && d == QLatin1String("recall");
        if (!frontHasSpanish && d != QLatin1String("cloze")) f.backSentenceHtml = withBold(w.sentence, w.text);
        if (d == QLatin1String("cloze")) f.backSentenceHtml = withBold(w.sentence, w.text);
        if (!w.sentenceTranslation.isEmpty() && !frontHasEnglish)
            f.backSentenceHtml += (f.backSentenceHtml.isEmpty() ? QString() : QStringLiteral("<br>")) +
                                  QStringLiteral("<span style='color:%1'>%2</span>").arg(muted.name(), w.sentenceTranslation.toHtmlEscaped());
    }
    f.source = w.bookTitle;
    if (!w.pageLabel.isEmpty()) f.source += (f.source.isEmpty() ? QString() : QStringLiteral(" · ")) + QObject::tr("p. %1").arg(w.pageLabel);
    return f;
}

QStringList acceptedAnswers(const WordEntry& w, const QString& d)
{
    if (d == QLatin1String("recognize")) return QStringList{w.translation} + w.alternatives;
    if (d == QLatin1String("cloze")) return {w.text, w.lemma};
    return {w.headword(), w.text};
}

QString answerLanguage(const WordEntry& w, const QString& d) { return d == QLatin1String("recognize") ? QStringLiteral("en") : w.language; }

PracticeCard answerCard(WordLog& log, const Scheduler& scheduler, QList<PracticeCard>& queue, const PracticeCard& card, Rating r,
                        const QDateTime& now)
{
    PracticeCard after = card;
    after.memory = scheduler.review(card.memory, r, now);
    log.saveReview(card, after, r, now);
    // A card forgotten again and again (8 times) is set aside: it needs a better meaning or a note, not more drill.
    const bool leech = r == Rating::Again && after.memory.lapses >= 8;
    if (leech) {
        log.setSuspended(after.id, true);
        after.suspended = true;
    }
    // Back within the next 20 minutes (Again, learning steps): later in this session.
    if (!leech && after.memory.due <= now.addSecs(20 * 60)) {
        int at = int(queue.size());
        // Not straight away: at least a few cards later when there are some.
        if (queue.size() > 3) at = std::min(int(queue.size()), 3 + int(queue.size()) / 3);
        queue.insert(at, after);
    }
    return after;
}

bool Session::start(const WordLog::Filter& filter, const QString& order, const QDateTime& now)
{
    clear();
    if (!m_log) return false;
    m_queue = m_log->allCards(filter, order, now);
    return next();
}

bool Session::next()
{
    m_card = {};
    m_word = {};
    while (!m_queue.isEmpty()) {
        m_card = m_queue.takeFirst();
        m_word = m_log->word(m_card.wordId);
        if (m_word.id != 0) return true;   // else deleted meanwhile
    }
    m_card = {};
    return false;
}

void Session::rate(Rating rating, const QDateTime& now)
{
    if (!m_log || m_card.id == 0) return;
    answerCard(*m_log, m_scheduler, m_queue, m_card, rating, now);
    ++m_done;
    ++m_ratings[int(rating)];
    next();
}

void Session::reloadWord()
{
    if (m_log && m_word.id) m_word = m_log->word(m_word.id);
}

void Session::clear()
{
    m_queue.clear();
    m_card = {};
    m_word = {};
    m_done = 0;
    std::fill(std::begin(m_ratings), std::end(m_ratings), 0);
}

}  // namespace core::practice

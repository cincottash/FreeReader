// Checking a typed answer: forgiving about case, spaces, punctuation and articles ("el árbol" = "árbol",
// "to have" = "have"), and telling apart a wrong answer, a missing accent and a small typo.
#pragma once

#include <QString>
#include <QStringList>

namespace core::practice {

enum class AnswerMatch {
    Wrong,
    Typo,         // one letter off in a longer word
    Accents,      // right except for accents or ñ
    Correct,
};

struct AnswerCheck {
    AnswerMatch match = AnswerMatch::Wrong;
    QString expected;   // the accepted answer closest to what was typed
};

// `accepted`: every answer that counts ("rack", "stand"); each may itself list several ("rack, stand; shelf").
AnswerCheck checkAnswer(const QString& typed, const QStringList& accepted, const QString& language);

// The separate answers in a meaning ("rack (for lances), stand" -> "rack", "stand").
QStringList splitMeanings(const QString& meaning);
QString foldAccents(const QString& s);   // "Árbol, niño" -> "arbol, nino"

}  // namespace core::practice

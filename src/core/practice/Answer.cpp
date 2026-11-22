#include "Answer.h"

#include <QRegularExpression>

#include <algorithm>
#include <vector>

namespace core::practice {

namespace {

// \w and \b must know that "á" and "ñ" are letters.
constexpr auto kUnicode = QRegularExpression::UseUnicodePropertiesOption;

// Leading words that don't decide whether an answer is right.
QStringList articles(const QString& language)
{
    if (language == QLatin1String("es")) return {"el", "la", "los", "las", "un", "una", "unos", "unas", "lo"};
    if (language == QLatin1String("fr")) return {"le", "la", "les", "un", "une", "des", "l"};
    if (language == QLatin1String("it")) return {"il", "lo", "la", "i", "gli", "le", "un", "uno", "una", "l"};
    if (language == QLatin1String("pt")) return {"o", "a", "os", "as", "um", "uma"};
    if (language == QLatin1String("de")) return {"der", "die", "das", "ein", "eine"};
    return {"the", "a", "an", "to"};
}

QString normalize(const QString& s, const QString& language, bool keepAccents)
{
    QString t = s.toLower();
    t.remove(QRegularExpression(QStringLiteral("\\([^)]*\\)")));   // "rack (for lances)" -> "rack"
    t.replace(QRegularExpression(QStringLiteral("[^\\w\\s'-]"), kUnicode), QStringLiteral(" "));
    t.replace(QLatin1Char('-'), QLatin1Char(' '));
    t.replace(QLatin1Char('\''), QLatin1Char(' '));
    QStringList words = t.split(QRegularExpression(QStringLiteral("\\s+"), kUnicode), Qt::SkipEmptyParts);
    const QStringList arts = articles(language) + articles(QStringLiteral("en"));
    while (words.size() > 1 && arts.contains(words.first())) words.removeFirst();
    t = words.join(QLatin1Char(' '));
    return keepAccents ? t : foldAccents(t);
}

int distance(const QString& a, const QString& b)
{
    std::vector<int> prev(size_t(b.size()) + 1), cur(size_t(b.size()) + 1);
    for (int j = 0; j <= b.size(); ++j) prev[size_t(j)] = j;
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            cur[size_t(j)] = std::min({prev[size_t(j)] + 1, cur[size_t(j - 1)] + 1, prev[size_t(j - 1)] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[size_t(b.size())];
}

}  // namespace

QString foldAccents(const QString& s)
{
    const QString d = s.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(d.size());
    for (const QChar c : d)
        if (c.category() != QChar::Mark_NonSpacing) out += c;
    return out;
}

QStringList splitMeanings(const QString& meaning)
{
    QStringList out;
    for (QString part : meaning.split(QRegularExpression(QStringLiteral("[,;/]|\\bor\\b|\\bo\\b"), kUnicode), Qt::SkipEmptyParts)) {
        part = part.trimmed();
        if (!part.isEmpty() && !out.contains(part, Qt::CaseInsensitive)) out << part;
    }
    if (out.isEmpty() && !meaning.trimmed().isEmpty()) out << meaning.trimmed();
    return out;
}

AnswerCheck checkAnswer(const QString& typed, const QStringList& accepted, const QString& language)
{
    AnswerCheck best;
    const QString exact = normalize(typed, language, true);
    if (exact.isEmpty()) {
        if (!accepted.isEmpty()) best.expected = splitMeanings(accepted.first()).value(0);
        return best;
    }
    const QString folded = foldAccents(exact);
    QStringList answers;
    for (const QString& a : accepted) answers << splitMeanings(a) << a.trimmed();
    for (const QString& a : answers) {
        if (a.isEmpty()) continue;
        const QString ae = normalize(a, language, true);
        if (ae.isEmpty()) continue;
        if (ae == exact) return {AnswerMatch::Correct, a};
        if (best.match < AnswerMatch::Accents && foldAccents(ae) == folded) best = {AnswerMatch::Accents, a};
        if (best.match < AnswerMatch::Typo && foldAccents(ae).size() >= 5 && distance(foldAccents(ae), folded) == 1) best = {AnswerMatch::Typo, a};
    }
    if (best.match == AnswerMatch::Wrong && !answers.isEmpty()) best.expected = answers.first();
    return best;
}

}  // namespace core::practice

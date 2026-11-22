#include "Hyphenator.h"

#include <QFile>
#include <QMutex>
#include <QRegularExpression>

#include <map>
#include <memory>

namespace layout {

namespace {

struct LangInfo {
    const char* code;
    const char* file;
    int leftMin, rightMin;   // the TeX defaults for each language
};

const LangInfo kLanguages[] = {
    {"es", "hyph-es", 2, 2}, {"en", "hyph-en-us", 2, 3}, {"fr", "hyph-fr", 2, 3},
    {"de", "hyph-de-1996", 2, 2}, {"it", "hyph-it", 2, 2}, {"pt", "hyph-pt", 2, 3},
};

constexpr char16_t kSoftHyphen = 0x00AD;

}  // namespace

const Hyphenator* Hyphenator::forLanguage(const QString& lang)
{
    static QMutex mutex;
    static std::map<QString, std::unique_ptr<Hyphenator>> cache;   // null entries: no patterns for that language
    const QString code = lang.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
    if (code.isEmpty()) return nullptr;
    QMutexLocker lock(&mutex);
    const auto it = cache.find(code);
    if (it != cache.end()) return it->second.get();
    std::unique_ptr<Hyphenator> h;
    for (const LangInfo& l : kLanguages) {
        if (code != QLatin1String(l.code)) continue;
        h = std::make_unique<Hyphenator>();
        if (!h->load(QString::fromLatin1(l.file), l.leftMin, l.rightMin)) h.reset();
    }
    const Hyphenator* raw = h.get();
    cache.emplace(code, std::move(h));
    return raw;
}

bool Hyphenator::load(const QString& name, int leftMin, int rightMin)
{
    m_leftMin = leftMin;
    m_rightMin = rightMin;
    QFile pat(QStringLiteral(":/hyphenation/%1.pat.txt").arg(name));
    if (!pat.open(QIODevice::ReadOnly)) return false;
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    for (const QString& p : QString::fromUtf8(pat.readAll()).split(ws, Qt::SkipEmptyParts)) {
        QString letters;
        QVector<quint8> values;
        values.append(0);
        for (QChar c : p) {
            if (c.isDigit()) {
                values.last() = quint8(c.digitValue());
            } else {
                letters += c;
                values.append(0);
            }
        }
        m_maxLen = qMax(m_maxLen, int(letters.size()));
        m_patterns.insert(letters, values);
    }
    QFile hyp(QStringLiteral(":/hyphenation/%1.hyp.txt").arg(name));
    if (hyp.open(QIODevice::ReadOnly)) {
        for (const QString& w : QString::fromUtf8(hyp.readAll()).split(ws, Qt::SkipEmptyParts)) {
            QString word;
            QList<int> at;
            for (QChar c : w) {
                if (c == u'-') at << int(word.size());
                else word += c;
            }
            if (!word.isEmpty()) m_exceptions.insert(word.toLower(), at);
        }
    }
    return !m_patterns.isEmpty();
}

QList<int> Hyphenator::breaks(QStringView word) const
{
    QList<int> out;
    const int n = int(word.size());
    if (n < m_leftMin + m_rightMin) return out;
    const QString lower = word.toString().toLower();
    const auto ex = m_exceptions.constFind(lower);
    if (ex != m_exceptions.cend()) return *ex;
    const QString w = QLatin1Char('.') + lower + QLatin1Char('.');
    const int len = int(w.size());
    QVector<quint8> v(len + 1, 0);
    for (int i = 0; i < len; ++i) {
        for (int l = 1; l <= qMin(m_maxLen, len - i); ++l) {
            const auto it = m_patterns.constFind(w.mid(i, l));
            if (it == m_patterns.cend()) continue;
            const QVector<quint8>& pv = *it;
            for (int k = 0; k < pv.size(); ++k) v[i + k] = qMax(v[i + k], pv[k]);
        }
    }
    // v[j + 1] is the value between word[j - 1] and word[j] (the leading '.' shifts by one).
    for (int j = m_leftMin; j <= n - m_rightMin; ++j)
        if (v[j + 1] % 2) out << j;
    return out;
}

QString Hyphenator::hyphenate(const QString& text, std::vector<int>* toLayout, std::vector<int>* toFlow) const
{
    const int n = int(text.size());
    QString out;
    out.reserve(n + n / 6);
    toLayout->assign(size_t(n) + 1, 0);
    toFlow->clear();
    toFlow->reserve(size_t(n + n / 6) + 1);
    int i = 0;
    while (i < n) {
        if (!text[i].isLetter()) {
            (*toLayout)[size_t(i)] = int(out.size());
            toFlow->push_back(i);
            out += text[i++];
            continue;
        }
        int j = i;
        bool hasShy = false;
        while (j < n && (text[j].isLetter() || text[j].isMark() || text[j] == QChar(kSoftHyphen))) {
            if (text[j] == QChar(kSoftHyphen)) hasShy = true;
            ++j;
        }
        // Words that already carry soft hyphens are left as the book set them.
        const QList<int> at = (hasShy || j - i < 5) ? QList<int>() : breaks(QStringView(text).mid(i, j - i));
        int next = 0;
        for (int k = i; k < j; ++k) {
            if (next < at.size() && at[next] == k - i) {
                toFlow->push_back(k);   // the inserted hyphen belongs to the character after it
                out += QChar(kSoftHyphen);
                ++next;
            }
            (*toLayout)[size_t(k)] = int(out.size());
            toFlow->push_back(k);
            out += text[k];
        }
        i = j;
    }
    (*toLayout)[size_t(n)] = int(out.size());
    toFlow->push_back(n);
    return out;
}

}  // namespace layout

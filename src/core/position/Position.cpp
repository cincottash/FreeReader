#include "Position.h"

#include "core/layout/Flow.h"

#include <QRegularExpression>

namespace core {

namespace {

QString withoutAssertions(const QString& path)
{
    static const QRegularExpression re(QStringLiteral("\\[[^\\]]*\\]"));
    QString p = path;
    p.remove(re);
    return p;
}

}  // namespace

QString makeCfi(int spine, const layout::ChapterFlow& flow, int offset)
{
    const layout::TextNodeRef* node = nullptr;
    int raw = 0;
    QString inner;
    if (layout::rawOffsetFor(flow, offset, &node, &raw)) inner = node->path + QLatin1Char(':') + QString::number(raw);
    else inner = QStringLiteral("/4");   // the body, for a chapter with no text
    // /6 is the package's spine; its itemrefs are its even-numbered children.
    return QStringLiteral("epubcfi(/6/%1!%2)").arg(2 * (spine + 1)).arg(inner);
}

bool parseCfi(const QString& cfi, int* spine, QString* path, int* rawOffset)
{
    static const QRegularExpression re(QStringLiteral("^epubcfi\\(/6/(\\d+)(?:\\[[^\\]]*\\])?!([^:)]*)(?::(\\d+))?.*\\)$"));
    const auto m = re.match(cfi.trimmed());
    if (!m.hasMatch()) return false;
    *spine = m.captured(1).toInt() / 2 - 1;
    *path = m.captured(2);
    *rawOffset = m.captured(3).isEmpty() ? 0 : m.captured(3).toInt();
    return *spine >= 0;
}

int resolveCfiPath(const layout::ChapterFlow& flow, const QString& path, int rawOffset)
{
    if (flow.textNodes.empty()) return 0;   // a chapter with no text (a cover): its start
    const QString want = withoutAssertions(path);
    for (const auto& n : flow.textNodes)
        if (withoutAssertions(n.path) == want) return layout::flowOffsetFor(n, rawOffset);
    // The text node moved: the first text inside the same element, then inside any ancestor.
    QString prefix = want;
    while (true) {
        const qsizetype slash = prefix.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0) break;
        prefix = prefix.left(slash);
        for (const auto& n : flow.textNodes)
            if (withoutAssertions(n.path).startsWith(prefix + QLatin1Char('/'))) return n.start;
    }
    return -1;
}

}  // namespace core

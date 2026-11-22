#include "Fonts.h"

#include "core/css/Css.h"
#include "core/format/Book.h"

#include <QFontDatabase>

namespace layout {

FontResolver::FontResolver()
{
    // Reading fonts first; then what Windows and the common Linux distributions ship.
    m_serif = {QStringLiteral("Literata"), QStringLiteral("Georgia"), QStringLiteral("Cambria"), QStringLiteral("Noto Serif"),
               QStringLiteral("DejaVu Serif"), QStringLiteral("Liberation Serif"), QStringLiteral("Times New Roman"), QStringLiteral("serif")};
    m_sans = {QStringLiteral("Inter"), QStringLiteral("Segoe UI"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"),
              QStringLiteral("Liberation Sans"), QStringLiteral("Arial"), QStringLiteral("sans-serif")};
    m_mono = {QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"),
              QStringLiteral("Liberation Mono"), QStringLiteral("Courier New"), QStringLiteral("monospace")};
}

FontResolver::~FontResolver()
{
    for (int id : m_fontIds) QFontDatabase::removeApplicationFont(id);
}

void FontResolver::addFontFaces(const core::Book& book, const css::StyleSheet& sheet)
{
    for (const css::FontFace& f : sheet.fontFaces) {
        QMutexLocker lock(&m_mutex);
        if (m_loadedFiles.contains(f.path)) {
            continue;
        }
        m_loadedFiles.insert(f.path, true);
        bool ok = false;
        const QByteArray data = book.resource(f.path, &ok);
        if (!ok || data.isEmpty()) continue;
        const int id = QFontDatabase::addApplicationFontFromData(data);
        if (id < 0) continue;
        m_fontIds << id;
        const QStringList fam = QFontDatabase::applicationFontFamilies(id);
        if (!fam.isEmpty() && !m_embedded.contains(f.family.toLower())) m_embedded.insert(f.family.toLower(), fam.first());
    }
}

QStringList FontResolver::resolve(const QStringList& cssFamilies) const
{
    QStringList out;
    bool generic = false;
    QStringList tail = m_serif;
    {
        QMutexLocker lock(&m_mutex);
        for (const QString& f : cssFamilies) {
            const QString k = f.toLower();
            if (k == QLatin1String("serif")) {
                out << (m_body.isEmpty() ? m_serif : m_body);
                generic = true;
                break;
            }
            if (k == QLatin1String("sans-serif")) {
                out << m_sans;
                generic = true;
                break;
            }
            if (k == QLatin1String("monospace")) {
                out << m_mono;
                generic = true;
                break;
            }
            if (k == QLatin1String("cursive") || k == QLatin1String("fantasy") || k == QLatin1String("system-ui")) continue;
            const auto it = m_embedded.constFind(k);
            out << (it != m_embedded.cend() ? *it : f);
        }
    }
    // The reader's own font replaces the book's fonts for all text but code.
    if (!m_body.isEmpty() && !(out.size() >= m_mono.size() && out.mid(0, m_mono.size()) == m_mono)) return m_body + m_serif;
    if (!generic) out << tail;
    return out;
}

}  // namespace layout

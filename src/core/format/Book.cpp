#include "Book.h"
#include "FormatPlugin.h"

#include "core/css/Css.h"
#include "core/format/epub/EpubFormat.h"

#include <QFile>
#include <QFileInfo>

namespace core {

int Book::spineIndexOf(const QString& path) const
{
    for (size_t i = 0; i < spine.size(); ++i)
        if (spine[i].path == path) return int(i);
    // Some books differ in case between the manifest and their links.
    for (size_t i = 0; i < spine.size(); ++i)
        if (spine[i].path.compare(path, Qt::CaseInsensitive) == 0) return int(i);
    return -1;
}

std::shared_ptr<const css::StyleSheet> Book::styleSheet(const QString& path) const
{
    {
        QMutexLocker lock(&m_cssMutex);
        const auto it = m_css.constFind(path);
        if (it != m_css.cend()) return *it;
    }
    auto sheet = std::make_shared<css::StyleSheet>();
    bool ok = false;
    const QByteArray data = resource(path, &ok);
    if (ok) {
        QString text = QString::fromUtf8(data);
        if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);
        sheet->parse(text, css::dirOf(path));
    }
    QMutexLocker lock(&m_cssMutex);
    m_css.insert(path, sheet);
    return sheet;
}

void registerBuiltInFormats()
{
    static bool done = false;
    if (done) return;
    done = true;
    FormatRegistry::instance().add(std::make_unique<EpubFormat>());
}

FormatRegistry& FormatRegistry::instance()
{
    static FormatRegistry r;
    return r;
}

void FormatRegistry::add(std::unique_ptr<FormatPlugin> plugin) { m_plugins.push_back(std::move(plugin)); }

const FormatPlugin* FormatRegistry::pluginFor(const QString& path) const
{
    QFile f(path);
    QByteArray head;
    if (f.open(QIODevice::ReadOnly)) head = f.read(4096);
    for (const auto& p : m_plugins)
        if (p->canOpen(path, head)) return p.get();
    return nullptr;
}

QStringList FormatRegistry::fileSuffixes() const
{
    QStringList out;
    for (const auto& p : m_plugins) out << p->fileSuffixes();
    return out;
}

std::shared_ptr<Book> FormatRegistry::open(const QString& path, QString* error) const
{
    if (!QFileInfo::exists(path)) {
        if (error) *error = QStringLiteral("The file doesn't exist.");
        return nullptr;
    }
    const FormatPlugin* p = pluginFor(path);
    if (!p) {
        if (error) *error = QStringLiteral("This file type isn't supported yet. Supported: %1.").arg(fileSuffixes().join(QStringLiteral(", ")).toUpper());
        return nullptr;
    }
    return p->open(path, error);
}

}  // namespace core

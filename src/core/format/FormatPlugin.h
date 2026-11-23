// Format plug-ins: each turns one file format into a Book. EPUB is the first; PDF, MOBI/AZW3 and plain text
// come later through the same interface.
#pragma once

#include "Book.h"

#include <memory>
#include <vector>

namespace core {

class FormatPlugin {
public:
    virtual ~FormatPlugin() = default;
    virtual QString name() const = 0;                    // "EPUB"
    virtual QStringList fileSuffixes() const = 0;        // {"epub"}
    // Cheap check from the first bytes of the file.
    virtual bool canOpen(const QString& path, const QByteArray& head) const = 0;
    // Opens the file. On failure returns null and a message for the user.
    virtual std::shared_ptr<Book> open(const QString& path, QString* error) const = 0;
    // For adding to the library: a book with at least its metadata and cover. A format that is slow to read
    // whole (a long PDF) gives only those; the others open the whole book.
    virtual std::shared_ptr<Book> openDetails(const QString& path, QString* error) const { return open(path, error); }
};

class FormatRegistry {
public:
    static FormatRegistry& instance();
    void add(std::unique_ptr<FormatPlugin> plugin);
    const FormatPlugin* pluginFor(const QString& path) const;
    QStringList fileSuffixes() const;
    // Picks the plug-in from the file's bytes, then opens it.
    std::shared_ptr<Book> open(const QString& path, QString* error) const;
    std::shared_ptr<Book> openDetails(const QString& path, QString* error) const;   // see FormatPlugin::openDetails

private:
    std::vector<std::unique_ptr<FormatPlugin>> m_plugins;
};

// Adds the formats built into the app (EPUB). Call once at start.
void registerBuiltInFormats();

}  // namespace core

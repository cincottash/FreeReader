// The common shape every format plug-in produces: metadata, table of contents, chapters (spine) as HTML plus
// resources. A Book is immutable once opened and safe to read from several threads.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace css {
class StyleSheet;
}

namespace core {

struct BookMetadata {
    QString title;
    QStringList authors;
    QString language;      // BCP 47 tag from the book ("es", "en-US"); may be empty
    QString identifier;    // the book's unique identifier (ISBN, UUID, URL)
    QString publisher;
    QString description;
    QString coverPath;     // resource path of the cover image, if the book names one
};

struct TocEntry {
    QString title;
    QString href;          // resource path plus optional "#fragment"
    int depth = 0;         // 0 = top level
    int parent = -1;       // index of the parent entry, -1 at the top
};

struct PageListEntry {
    QString label;         // "12", "xii"
    QString href;
};

struct SpineItem {
    QString path;          // resource path of the chapter's (X)HTML
    QString id;            // manifest id (the itemref's idref)
    bool linear = true;
};

class Book {
public:
    virtual ~Book() = default;

    BookMetadata metadata;
    std::vector<SpineItem> spine;
    std::vector<TocEntry> toc;           // flattened, in reading order, with depth and parent
    std::vector<PageListEntry> pageList; // the publisher's print page numbers, when the book has them
    QString filePath;

    virtual QByteArray resource(const QString& path, bool* ok = nullptr) const = 0;
    virtual bool hasResource(const QString& path) const = 0;

    int spineIndexOf(const QString& path) const;
    // A style sheet of the book, parsed once and shared (thread-safe).
    std::shared_ptr<const css::StyleSheet> styleSheet(const QString& path) const;

private:
    mutable QMutex m_cssMutex;
    mutable QHash<QString, std::shared_ptr<const css::StyleSheet>> m_css;
};

}  // namespace core

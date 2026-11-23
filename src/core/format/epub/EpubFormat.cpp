#include "EpubFormat.h"

#include "core/css/Css.h"
#include "core/html/HtmlParser.h"
#include "core/zip/ZipArchive.h"

#include <QFileInfo>

#include <functional>

namespace core {

namespace {

// The message for a DRM-protected book: which protection, the store it came from when the licence says, and
// where it can be read. Nothing is decrypted.
QString drmMessage(const ZipArchive& zip)
{
    bool ok = false;
    const QByteArray rights = zip.read(QStringLiteral("META-INF/rights.xml"), &ok);
    if (!ok) return QStringLiteral("This book is protected by DRM, so FreeReader can't open it. Only DRM-free books can be opened.");
    QString store;
    if (rights.contains("books.google.com")) store = QStringLiteral("Google Play Books");
    else if (rights.contains("kobo")) store = QStringLiteral("Kobo");
    else if (rights.contains("barnesandnoble") || rights.contains("nook")) store = QStringLiteral("Barnes & Noble");
    QString text = QStringLiteral("This book is protected by Adobe DRM");
    if (!store.isEmpty()) text += QStringLiteral(" from %1").arg(store);
    text += QStringLiteral(", so FreeReader can't open it. It can be read in ");
    text += store == QLatin1String("Google Play Books") ? QStringLiteral("the Google Play Books app or website, or in Adobe Digital Editions.")
          : store == QLatin1String("Kobo")             ? QStringLiteral("the Kobo app, or in Adobe Digital Editions.")
                                                       : QStringLiteral("Adobe Digital Editions.");
    text += QStringLiteral(" Some stores sell books without DRM; their pages say \"DRM-free\".");
    return text;
}

class EpubBook : public Book {
public:
    ZipArchive zip;
    QSet<QString> skipped;   // obfuscated fonts: left out, the reader's fonts are used instead

    QByteArray resource(const QString& path, bool* ok) const override
    {
        if (skipped.contains(path)) {
            if (ok) *ok = false;
            return {};
        }
        return zip.read(path, ok);
    }
    bool hasResource(const QString& path) const override { return zip.contains(path) && !skipped.contains(path); }
};

QString childText(const dom::Node* n, const char* name)
{
    if (!n) return {};
    const dom::Node* c = n->firstElement(name);
    return c ? c->textContent().simplified() : QString();
}

// "chapter1.xhtml#s2" relative to the folder of the file that links it -> "OEBPS/chapter1.xhtml#s2".
QString resolveHref(const QString& baseDir, const QString& href)
{
    const qsizetype hash = href.indexOf(QLatin1Char('#'));
    const QString frag = hash >= 0 ? href.mid(hash) : QString();
    const QString file = hash >= 0 ? href.left(hash) : href;
    if (file.isEmpty()) return {};
    return css::resolvePath(baseDir, file) + frag;
}

void readNavList(const dom::Node* ol, const QString& baseDir, int depth, int parent, std::vector<TocEntry>& out)
{
    if (!ol) return;
    for (const dom::Node* li : ol->elements("li")) {
        TocEntry e;
        e.depth = depth;
        e.parent = parent;
        const dom::Node* a = li->firstElement("a");
        const dom::Node* label = a ? a : li->firstElement("span");
        if (!label) label = li;
        e.title = label->textContent().simplified();
        if (a) e.href = resolveHref(baseDir, a->attr("href"));
        int self = parent;
        if (!e.title.isEmpty()) {
            out.push_back(e);
            self = int(out.size()) - 1;
        }
        readNavList(li->firstElement("ol"), baseDir, e.title.isEmpty() ? depth : depth + 1, self, out);
    }
}

void readNcxPoints(const dom::Node* parentNode, const QString& baseDir, int depth, int parent, std::vector<TocEntry>& out)
{
    for (const dom::Node* p : parentNode->elements("navpoint")) {
        TocEntry e;
        e.depth = depth;
        e.parent = parent;
        e.title = childText(p->firstElement("navlabel"), "text");
        if (const dom::Node* c = p->firstElement("content")) e.href = resolveHref(baseDir, c->attr("src"));
        out.push_back(e);
        readNcxPoints(p, baseDir, depth + 1, int(out.size()) - 1, out);
    }
}

const dom::Node* findNav(const dom::Node* n, const QString& type)
{
    if (n->is("nav") && n->attrHasWord("epub:type", type)) return n;
    for (const auto& c : n->children)
        if (c->isElement())
            if (const dom::Node* f = findNav(c.get(), type)) return f;
    return nullptr;
}

}  // namespace

bool EpubFormat::canOpen(const QString& path, const QByteArray& head) const
{
    // A zip holding application/epub+zip, or any file named .epub (a damaged one then gets a precise message).
    if (QFileInfo(path).suffix().compare(QLatin1String("epub"), Qt::CaseInsensitive) == 0) return true;
    return head.startsWith("PK\x03\x04") && head.contains("application/epub+zip");
}

std::shared_ptr<Book> EpubFormat::open(const QString& path, QString* error) const
{
    auto book = std::make_shared<EpubBook>();
    book->filePath = path;
    QString zipError;
    if (!book->zip.open(path, &zipError)) {
        if (error) *error = zipError;
        return nullptr;
    }

    // DRM: Adobe ADEPT and other schemes add rights.xml or encrypt the chapters. Font obfuscation is not DRM,
    // but the obfuscated fonts are simply not used.
    if (book->zip.contains(QStringLiteral("META-INF/rights.xml"))) {
        if (error) *error = drmMessage(book->zip);
        return nullptr;
    }
    bool ok = false;
    const QByteArray enc = book->zip.read(QStringLiteral("META-INF/encryption.xml"), &ok);
    if (ok) {
        const auto doc = dom::parse(enc, dom::ParseMode::Xml);
        std::function<void(const dom::Node*)> walk = [&](const dom::Node* n) {
            if (n->is("encrypteddata") || n->name.endsWith(QLatin1String(":encrypteddata"))) {
                QString algorithm, uri;
                std::function<void(const dom::Node*)> inner = [&](const dom::Node* m) {
                    if (m->name.endsWith(QLatin1String("encryptionmethod"))) algorithm = m->attr("algorithm");
                    if (m->name.endsWith(QLatin1String("cipherreference"))) uri = m->attr("uri");
                    for (const auto& c : m->children) inner(c.get());
                };
                inner(n);
                const bool fontObfuscation = algorithm == QLatin1String("http://www.idpf.org/2008/embedding")
                                             || algorithm == QLatin1String("http://ns.adobe.com/pdf/enc#RC");
                if (fontObfuscation) book->skipped.insert(css::resolvePath(QString(), uri));
                else if (!uri.isEmpty()) book->skipped.insert(QStringLiteral("<drm>"));
            }
            for (const auto& c : n->children) walk(c.get());
        };
        walk(doc->root.get());
        if (book->skipped.contains(QStringLiteral("<drm>"))) {
            if (error) *error = drmMessage(book->zip);
            return nullptr;
        }
    }
    if (book->zip.hasEncryptedEntries()) {
        if (error) *error = QStringLiteral("This book's files are encrypted, so it can't be opened.");
        return nullptr;
    }

    // container.xml -> the package (OPF) file.
    const QByteArray containerData = book->zip.read(QStringLiteral("META-INF/container.xml"), &ok);
    QString opfPath;
    if (ok) {
        const auto doc = dom::parse(containerData, dom::ParseMode::Xml);
        std::function<void(const dom::Node*)> walk = [&](const dom::Node* n) {
            if (opfPath.isEmpty() && n->is("rootfile")) {
                const QString mt = n->attr("media-type");
                if (mt.isEmpty() || mt == QLatin1String("application/oebps-package+xml")) opfPath = n->attr("full-path");
            }
            for (const auto& c : n->children) walk(c.get());
        };
        walk(doc->root.get());
    }
    if (opfPath.isEmpty()) {
        for (const QString& n : book->zip.names())
            if (n.endsWith(QLatin1String(".opf"), Qt::CaseInsensitive)) {
                opfPath = n;
                break;
            }
    }
    const QByteArray opfData = book->zip.read(opfPath, &ok);
    if (!ok) {
        if (error) *error = QStringLiteral("The book is damaged: its package file is missing.");
        return nullptr;
    }
    const QString opfDir = css::dirOf(opfPath);
    const auto opf = dom::parse(opfData, dom::ParseMode::Xml);
    const dom::Node* package = opf->documentElement();
    if (!package) {
        if (error) *error = QStringLiteral("The book is damaged: its package file can't be read.");
        return nullptr;
    }

    // Metadata
    const dom::Node* meta = package->firstElement("metadata");
    const QString uniqueId = package->attr("unique-identifier");
    QString coverId;
    if (meta) {
        for (const auto& c : meta->children) {
            if (!c->isElement()) continue;
            const QString n = c->name;
            const QString text = c->textContent().simplified();
            if (n == QLatin1String("dc:title") && book->metadata.title.isEmpty()) book->metadata.title = text;
            else if (n == QLatin1String("dc:creator") && !text.isEmpty()) book->metadata.authors << text;
            else if (n == QLatin1String("dc:language") && book->metadata.language.isEmpty()) book->metadata.language = text;
            else if (n == QLatin1String("dc:publisher") && book->metadata.publisher.isEmpty()) book->metadata.publisher = text;
            else if (n == QLatin1String("dc:description") && book->metadata.description.isEmpty()) book->metadata.description = text;
            else if (n == QLatin1String("dc:identifier")) {
                if (book->metadata.identifier.isEmpty() || c->id() == uniqueId) book->metadata.identifier = text;
            } else if (n == QLatin1String("meta") && c->attr("name") == QLatin1String("cover")) {
                coverId = c->attr("content");
            }
        }
    }
    if (book->metadata.language.isEmpty()) book->metadata.language = package->attr("xml:lang");

    // Manifest
    struct Item {
        QString path, mediaType, properties;
    };
    QHash<QString, Item> manifest;
    QString navPath, ncxPath;
    if (const dom::Node* man = package->firstElement("manifest")) {
        for (const dom::Node* it : man->elements("item")) {
            Item item{css::resolvePath(opfDir, it->attr("href")), it->attr("media-type"), it->attr("properties")};
            manifest.insert(it->id(), item);
            if (item.properties.split(QLatin1Char(' ')).contains(QLatin1String("nav"))) navPath = item.path;
            if (item.properties.split(QLatin1Char(' ')).contains(QLatin1String("cover-image"))) book->metadata.coverPath = item.path;
            if (item.mediaType == QLatin1String("application/x-dtbncx+xml")) ncxPath = item.path;
        }
    }
    if (book->metadata.coverPath.isEmpty() && manifest.contains(coverId)) book->metadata.coverPath = manifest.value(coverId).path;

    // Spine
    if (const dom::Node* spine = package->firstElement("spine")) {
        if (ncxPath.isEmpty() && manifest.contains(spine->attr("toc"))) ncxPath = manifest.value(spine->attr("toc")).path;
        for (const dom::Node* ref : spine->elements("itemref")) {
            const auto it = manifest.constFind(ref->attr("idref"));
            if (it == manifest.cend() || !book->zip.contains(it->path)) continue;
            SpineItem s;
            s.path = it->path;
            s.id = ref->attr("idref");
            s.linear = ref->attr("linear") != QLatin1String("no");
            book->spine.push_back(s);
        }
    }
    if (book->spine.empty()) {
        if (error) *error = QStringLiteral("The book has no chapters to show.");
        return nullptr;
    }

    // Table of contents and page list: EPUB 3 navigation document first, NCX as the fallback.
    if (!navPath.isEmpty()) {
        const QByteArray navData = book->zip.read(navPath, &ok);
        if (ok) {
            const auto nav = dom::parse(navData);
            const QString navDir = css::dirOf(navPath);
            if (const dom::Node* toc = findNav(nav->root.get(), QStringLiteral("toc")))
                readNavList(toc->firstElement("ol"), navDir, 0, -1, book->toc);
            if (const dom::Node* pl = findNav(nav->root.get(), QStringLiteral("page-list"))) {
                std::vector<TocEntry> pages;
                readNavList(pl->firstElement("ol"), navDir, 0, -1, pages);
                for (const TocEntry& p : pages) book->pageList.push_back({p.title, p.href});
            }
        }
    }
    if (!ncxPath.isEmpty() && (book->toc.empty() || book->pageList.empty())) {
        const QByteArray ncxData = book->zip.read(ncxPath, &ok);
        if (ok) {
            const auto ncx = dom::parse(ncxData, dom::ParseMode::Xml);
            const dom::Node* root = ncx->documentElement();
            const QString ncxDir = css::dirOf(ncxPath);
            if (root && book->toc.empty())
                if (const dom::Node* map = root->firstElement("navmap")) readNcxPoints(map, ncxDir, 0, -1, book->toc);
            if (root && book->pageList.empty())
                if (const dom::Node* pl = root->firstElement("pagelist"))
                    for (const dom::Node* t : pl->elements("pagetarget")) {
                        const dom::Node* c = t->firstElement("content");
                        if (c) book->pageList.push_back({childText(t->firstElement("navlabel"), "text"), resolveHref(ncxDir, c->attr("src"))});
                    }
        }
    }
    if (book->metadata.title.isEmpty()) book->metadata.title = QFileInfo(path).completeBaseName();
    return book;
}

}  // namespace core

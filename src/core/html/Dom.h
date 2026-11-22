// A small document tree for a book's XHTML/HTML chapters and its XML files (OPF, NCX, navigation).
// Element and attribute names keep their prefix ("epub:type", "xlink:href"); element names are lowercase,
// except that the namespace prefix of an element ("svg:image", "opf:package") is dropped.
#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

#include <memory>
#include <vector>

namespace dom {

struct Node {
    enum Type { Element, Text };
    Type type = Element;
    QString name;                              // elements: local name, lowercase
    QList<QPair<QString, QString>> attrs;      // in document order
    QString text;                              // text nodes: decoded text
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
    int elementIndex = 0;                      // index among the parent's element children
    int indexInParent = 0;                     // index among all of the parent's children

    bool isElement() const { return type == Element; }
    bool isText() const { return type == Text; }
    bool is(const char* n) const { return type == Element && name == QLatin1String(n); }
    QString attr(const QString& n) const;
    QString attr(const char* n) const { return attr(QLatin1String(n)); }
    bool hasAttr(const char* n) const;
    QString id() const { return attr("id"); }
    // Whitespace-separated words of an attribute (class, epub:type).
    bool attrHasWord(const char* n, const QString& word) const;

    Node* firstElement(const char* name) const;            // first child element with this name
    std::vector<Node*> elements(const char* name) const;   // child elements with this name
    Node* findDescendant(const char* name) const;          // depth first
    QString textContent() const;
    Node* previousElementSibling() const;
};

struct Document {
    std::unique_ptr<Node> root = std::make_unique<Node>();   // a synthetic root around the document element
    Node* documentElement() const;
    Node* byId(const QString& id) const;                     // built on first use
    Node* body() const;
    Node* head() const;

private:
    mutable QHash<QString, Node*> m_ids;
    mutable bool m_idsBuilt = false;
};

}  // namespace dom

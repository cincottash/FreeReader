#include "Dom.h"

#include <functional>

namespace dom {

QString Node::attr(const QString& n) const
{
    for (const auto& a : attrs)
        if (a.first == n) return a.second;
    return {};
}

bool Node::hasAttr(const char* n) const
{
    for (const auto& a : attrs)
        if (a.first == QLatin1String(n)) return true;
    return false;
}

bool Node::attrHasWord(const char* n, const QString& word) const
{
    const QString v = attr(n);
    if (v.isEmpty()) return false;
    for (const auto& w : QStringView(v).split(u' ', Qt::SkipEmptyParts))
        if (w.trimmed() == word) return true;
    return false;
}

Node* Node::firstElement(const char* n) const
{
    for (const auto& c : children)
        if (c->is(n)) return c.get();
    return nullptr;
}

std::vector<Node*> Node::elements(const char* n) const
{
    std::vector<Node*> out;
    for (const auto& c : children)
        if (c->is(n)) out.push_back(c.get());
    return out;
}

Node* Node::findDescendant(const char* n) const
{
    for (const auto& c : children) {
        if (c->is(n)) return c.get();
        if (Node* d = c->findDescendant(n)) return d;
    }
    return nullptr;
}

QString Node::textContent() const
{
    if (isText()) return text;
    QString out;
    for (const auto& c : children) out += c->textContent();
    return out;
}

Node* Node::previousElementSibling() const
{
    if (!parent) return nullptr;
    for (int i = indexInParent - 1; i >= 0; --i)
        if (parent->children[size_t(i)]->isElement()) return parent->children[size_t(i)].get();
    return nullptr;
}

Node* Document::documentElement() const
{
    for (const auto& c : root->children)
        if (c->isElement()) return c.get();
    return nullptr;
}

Node* Document::byId(const QString& id) const
{
    if (!m_idsBuilt) {
        m_idsBuilt = true;
        std::function<void(Node*)> walk = [&](Node* n) {
            if (n->isElement()) {
                const QString i = n->id();
                if (!i.isEmpty() && !m_ids.contains(i)) m_ids.insert(i, n);
                // Old-style anchors: <a name="x">.
                if (n->is("a")) {
                    const QString nm = n->attr("name");
                    if (!nm.isEmpty() && !m_ids.contains(nm)) m_ids.insert(nm, n);
                }
            }
            for (const auto& c : n->children) walk(c.get());
        };
        walk(root.get());
    }
    return m_ids.value(id, nullptr);
}

Node* Document::body() const
{
    Node* html = documentElement();
    if (!html) return nullptr;
    if (html->is("body")) return html;
    if (Node* b = html->firstElement("body")) return b;
    return html->findDescendant("body");
}

Node* Document::head() const
{
    Node* html = documentElement();
    return html ? html->firstElement("head") : nullptr;
}

}  // namespace dom

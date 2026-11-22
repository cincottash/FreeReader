// A forgiving parser for book files. EPUB chapters should be well-formed XHTML, but real books are often not:
// this parser never fails, closes unclosed elements, ignores stray end tags and knows the HTML void elements.
#pragma once

#include "Dom.h"

#include <QByteArray>

#include <memory>

namespace dom {

enum class ParseMode {
    Html,   // chapters: void elements, implied end tags (</p>, </li>, </td>), raw text in <script> and <style>
    Xml     // OPF, NCX, container.xml: only explicit structure
};

// Decodes the bytes (UTF-8 unless an XML declaration, a byte order mark or <meta charset> says otherwise).
std::unique_ptr<Document> parse(const QByteArray& data, ParseMode mode = ParseMode::Html);
std::unique_ptr<Document> parseText(const QString& text, ParseMode mode = ParseMode::Html);

// Decodes "&amp;", "&#233;", "&eacute;" ... in a piece of text.
QString decodeEntities(QStringView s);

}  // namespace dom

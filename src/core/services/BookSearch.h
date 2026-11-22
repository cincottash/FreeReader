// Searching a book's text: every chapter's flow, ignoring case and accents unless asked ("razon" finds "razón"),
// optionally whole words only. Runs on a background thread (it builds each chapter's flow).
#pragma once

#include <QList>
#include <QString>

#include <atomic>

namespace core {

class Book;

struct SearchOptions {
    bool wholeWord = false;
    bool matchCase = false;
    bool matchAccents = false;
    int limit = 1000;
};

struct SearchHit {
    int spine = -1, start = 0, end = 0;   // chapter offsets of the match
    QString before, match, after;          // the match in its context (about 40 characters each side)
};

// Stops early (returning what it has) when `cancel` becomes true; `truncated` says the limit was reached.
QList<SearchHit> searchBook(const Book& book, const QString& query, const SearchOptions& options, const std::atomic<bool>* cancel = nullptr,
                            bool* truncated = nullptr);

// Case and accent folding that keeps every character's position ("Árbol" -> "arbol", same length).
QString foldForSearch(const QString& s, bool matchCase, bool matchAccents);

}  // namespace core

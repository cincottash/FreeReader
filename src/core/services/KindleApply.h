// Applying a Kindle import plan (core/services/KindleImport.h) to the open book, the same on the desktop and the
// phone: highlights (orange on this edition's equivalent words), words for the Word List, bookmarks, and the
// passages this edition doesn't have as bookmarks at about their place, so nothing is lost. Items already there are
// skipped. One database transaction for the whole import (thousands of rows).
#pragma once

#include "KindleImport.h"

#include "core/translate/Translation.h"

#include <QString>

#include <functional>
#include <memory>

namespace core {

class Annotations;
class Book;
class BookIndex;
class Library;
class WordLog;

struct KindleOptions {
    bool highlights = true, words = true, missingWords = true, missingPassages = true, bookmarks = true, moveToLastRead = true;
    bool equivalents = true;   // highlights this edition words differently: on their equivalent words, in orange
};

struct KindleResult {
    int highlights = 0, equivalents = 0, words = 0, missingWords = 0, passages = 0, bookmarks = 0, skipped = 0;
    bool moved = false;   // set by the caller when it went to the last place read
};

struct KindleTarget {
    const Book* book = nullptr;
    int bookId = 0;
    QString bookTitle, language;
    std::shared_ptr<const BookIndex> index;   // page labels (may be null)
    Annotations* notes = nullptr;
    WordLog* words = nullptr;                 // may be null: no words
    Library* library = nullptr;               // its database: one transaction
    std::function<TranslationResult(const QString& word)> offlineMeaning;   // may be empty
    std::function<QString(const Position&)> chapterTitle;                    // may be empty
};

KindleResult applyKindlePlan(const KindlePlan& plan, const KindleOptions& options, const KindleTarget& target);

}  // namespace core

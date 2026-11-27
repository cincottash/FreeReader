#include "HelpPanel.h"

#include "Icons.h"

#include "core/translate/LanguagePacks.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

struct Section {
    const char* anchor;
    const char* title;
};

// The guide's sections, in order (the contents at the top lists them).
const Section kSections[] = {
    {"start", QT_TRANSLATE_NOOP("HelpPanel", "First Steps")},
    {"reading", QT_TRANSLATE_NOOP("HelpPanel", "Reading")},
    {"translation", QT_TRANSLATE_NOOP("HelpPanel", "Translating Words and Sentences")},
    {"dictionary", QT_TRANSLATE_NOOP("HelpPanel", "Dictionaries")},
    {"highlights", QT_TRANSLATE_NOOP("HelpPanel", "Highlights, Notes and Bookmarks")},
    {"practice", QT_TRANSLATE_NOOP("HelpPanel", "Practice and the Word List")},
    {"library", QT_TRANSLATE_NOOP("HelpPanel", "The Library")},
    {"look", QT_TRANSLATE_NOOP("HelpPanel", "Text, Layout and Themes")},
    {"audio", QT_TRANSLATE_NOOP("HelpPanel", "Hearing Words Aloud")},
    {"settings", QT_TRANSLATE_NOOP("HelpPanel", "Common Settings")},
    {"faq", QT_TRANSLATE_NOOP("HelpPanel", "Questions and Answers")},
};

}  // namespace

HelpPanel::HelpPanel(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Panel"));
    setAttribute(Qt::WA_StyledBackground);
    setProperty("testId", "helpPanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 8, 0);
    top->setSpacing(2);
    auto* title = new QLabel(tr("GETTING STARTED"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    top->addWidget(title, 1);
    m_close = new QToolButton;
    m_close->setObjectName(QStringLiteral("Icon"));
    m_close->setFixedSize(32, 32);
    m_close->setIconSize(QSize(18, 18));
    m_close->setToolTip(tr("Close"));
    m_close->setCursor(Qt::PointingHandCursor);
    m_close->setFocusPolicy(Qt::NoFocus);
    m_close->setProperty("testId", "helpClose");
    top->addWidget(m_close);
    lay->addLayout(top);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search the Guide"));
    m_search->setClearButtonEnabled(true);
    m_search->setProperty("testId", "helpSearch");
    auto* searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(12, 0, 12, 6);
    searchRow->addWidget(m_search);
    lay->addLayout(searchRow);

    m_browser = new QTextBrowser;
    m_browser->setProperty("testId", "helpText");
    m_browser->setOpenLinks(false);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->document()->setDocumentMargin(14);
    lay->addWidget(m_browser, 1);

    connect(m_close, &QToolButton::clicked, this, &HelpPanel::closeRequested);
    connect(m_browser, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) { clickLink(url.toString()); });
    connect(m_search, &QLineEdit::returnPressed, this, [this] { find(m_search->text()); });
    connect(m_search, &QLineEdit::textEdited, this, [this](const QString& t) {
        // As the words are typed: the first place they appear.
        if (t.trimmed().size() < 3) return;
        m_browser->moveCursor(QTextCursor::Start);
        find(t);
    });
    setTheme(m_theme);
}

QStringList HelpPanel::sections() const
{
    QStringList out;
    for (const Section& s : kSections) out << QString::fromLatin1(s.anchor);
    return out;
}

QString HelpPanel::text() const { return m_browser->toPlainText(); }

void HelpPanel::showSection(const QString& anchor)
{
    if (anchor.isEmpty()) m_browser->moveCursor(QTextCursor::Start);
    else m_browser->scrollToAnchor(anchor);
}

bool HelpPanel::find(const QString& words)
{
    const QString w = words.trimmed();
    if (w.isEmpty()) return false;
    if (m_browser->find(w)) return true;
    // From the top again.
    m_browser->moveCursor(QTextCursor::Start);
    return m_browser->find(w);
}

void HelpPanel::clickLink(const QString& href)
{
    if (href.startsWith(QLatin1String("run:"))) emit commandRequested(href.mid(4));
    else if (href.startsWith(QLatin1Char('#'))) m_browser->scrollToAnchor(href.mid(1));
    else if (href.startsWith(QLatin1String("https://"))) QDesktopServices::openUrl(QUrl(href));
}

void HelpPanel::setTheme(const Theme& t)
{
    m_theme = t;
    setStyleSheet(QStringLiteral("#Panel { border: 0; border-left: 1px solid %1; }").arg(t.line.name()));
    m_close->setIcon(icons::icon("close", t.fg));
    m_browser->setStyleSheet(QStringLiteral("QTextBrowser { background: %1; color: %2; border: 0; }").arg(t.surface.name(), t.fg.name()));
    const int y = m_browser->verticalScrollBar() ? m_browser->verticalScrollBar()->value() : 0;
    m_browser->setHtml(html());
    if (m_browser->verticalScrollBar()) m_browser->verticalScrollBar()->setValue(y);
}

QString HelpPanel::html() const
{
    const QString accent = m_theme.accent.name(), muted = m_theme.muted.name(), line = m_theme.line.name();
    QString h;
    h += QStringLiteral("<style>"
                        "h2 { font-size: 15px; margin-top: 18px; margin-bottom: 4px; }"
                        "p, li { line-height: 135%; }"
                        "a { color: %1; text-decoration: none; }"
                        ".muted { color: %2; }"
                        ".key { font-weight: 600; }"
                        "</style>")
             .arg(accent, muted);
    auto section = [&](const char* anchor) {
        for (const Section& s : kSections)
            if (qstrcmp(s.anchor, anchor) == 0)
                h += QStringLiteral("<a name=\"%1\"></a><h2>%2</h2>").arg(QString::fromLatin1(anchor), tr(s.title));
    };
    auto link = [](const QString& command, const QString& text) {
        return QStringLiteral("<a href=\"run:%1\">%2</a>").arg(command, text);
    };
    auto p = [&](const QString& t) { h += QStringLiteral("<p>") + t + QStringLiteral("</p>"); };
    auto list = [&](const QStringList& items) {
        h += QStringLiteral("<ul style=\"margin-left: -18px;\">");
        for (const QString& i : items) h += QStringLiteral("<li>") + i + QStringLiteral("</li>");
        h += QStringLiteral("</ul>");
    };

    p(QStringLiteral("<span class=\"muted\">") +
      tr("FreeReader is an e-book reader built for reading in another language: right-click a word and its meaning "
         "appears beside it, and every word you look up becomes a flashcard.") +
      QStringLiteral("</span>"));
    // The contents.
    QStringList contents;
    for (const Section& s : kSections)
        contents << QStringLiteral("<a href=\"#%1\">%2</a>").arg(QString::fromLatin1(s.anchor), tr(s.title));
    h += QStringLiteral("<p>") + contents.join(QStringLiteral("<br>")) + QStringLiteral("</p>");
    h += QStringLiteral("<hr style=\"color: %1;\">").arg(line);

    section("start");
    list({tr("<b>Add your books:</b> %1 or %2, or drop files on the window. The files stay where they are.")
              .arg(link(QStringLiteral("add-books"), tr("Add Books…")), link(QStringLiteral("add-folder"), tr("Add Folder…"))),
          tr("<b>Open a book:</b> double-click its cover. FreeReader remembers your place."),
          tr("<b>Look up a word:</b> right-click it and choose <b>Translate</b>."),
          tr("<b>Practise the words:</b> the <b>Practice</b> button while reading, or <b>Practice Words</b> in the library."),
          tr("<b>Set up Gemini</b> for the best translations: a free key from Google, pasted in %1.")
              .arg(link(QStringLiteral("translation-settings"), tr("Translation Settings")))});

    section("reading");
    list({tr("<b>Turn pages</b> with the arrow keys, Page Up and Page Down, Space, the mouse wheel, or the arrows at the "
             "window's left and right edges."),
          tr("<b>Contents:</b> the list button in the header (Ctrl+T) shows the chapters."),
          tr("<b>Go to a page:</b> Ctrl+G, then a page number or a percentage such as 45%. Click the progress bar to jump. "
             "<b>Back</b> (Alt+Left) returns to where you were."),
          tr("<b>One page or two:</b> the two-pages button in the header shows two pages side by side, like an open book."),
          tr("<b>Pages or time left:</b> the footer shows the pages left in the chapter; click it for the time left."),
          tr("<b>Footnotes</b> open in a small pop-up. <b>Pictures</b> open full size when clicked."),
          tr("<b>Full screen:</b> F11. <b>Every command</b> with a search box: Ctrl+K (%1).")
              .arg(link(QStringLiteral("commands"), tr("Show Commands")))});

    section("translation");
    list({tr("<b>Translate:</b> right-click a word or a selection, then <b>Translate</b> (or select and press Ctrl+E). "
             "A word opens as a dictionary card all in your language: its parts of speech and meanings, the one in that "
             "sentence first; <b>Flip</b> shows its definitions in the book's language. A passage shows its translation."),
          tr("On the card: <b>Hear Pronunciation</b> and <b>Highlight</b>, which highlights the word without another "
             "right-click. A passage's pop-up also has <b>Copy</b>."),
          tr("<b>Explain This Sentence</b> (Ctrl+Shift+E): the sentence translated, with notes on its grammar and idioms."),
          tr("<b>Your language alongside:</b> %1 (Ctrl+Shift+B) translates every paragraph on the page in a panel beside it.")
              .arg(link(QStringLiteral("bilingual"), tr("Show %1 Alongside").arg(core::languages::displayName(core::languages::readerLanguage())))),
          tr("<b>Where answers come from,</b> in this order: Google's Gemini (online, free), LM Studio on your computer, "
             "your Word List, then the offline dictionaries and the offline translator. Each can be switched off in "
             "Settings ▸ Translation. When Gemini or LM Studio takes longer than its <b>Wait</b> time there over a word "
             "or passage (2.5 seconds at first), the next one answers instead."),
          tr("<b>Gemini for explanations only:</b> with <b>Use Gemini Only for Explain This Sentence</b> on (Settings ▸ "
             "Translation), Gemini answers only Explain This Sentence; words, passages and the paragraphs alongside come "
             "from the offline dictionaries and the offline translator (or LM Studio, when it's on and running)."),
          tr("<b>Keep text private:</b> Gemini is the only engine that sends text over the internet. Turn off <b>Use "
             "Gemini</b> for every book, or <b>Keep This Book Offline</b> for one.")});

    section("dictionary");
    list({tr("<b>Dictionary</b> (right-click a word): the word's whole entry, like a printed dictionary, with its forms, "
             "senses and examples. A verb form opens its verb."),
          tr("<b>The card turns over:</b> <b>Flip</b> at its bottom right shows the other side, one all in the book's "
             "language, the other in yours."),
          tr("<b>Offline dictionaries</b> answer single words with no internet. Download them in %1.")
              .arg(link(QStringLiteral("translation-settings"), tr("Translation Settings")))});

    section("highlights");
    list({tr("<b>Highlight:</b> the color dots at the top of the right-click menu. Right-click a highlight to change its "
             "color, add a note or remove it."),
          tr("<b>Notes:</b> right-click, <b>Add Note…</b>."),
          tr("<b>Bookmarks:</b> Ctrl+D bookmarks the page; right-click a word for <b>Bookmark Here</b>. <b>Go to Latest "
             "Bookmark</b> (Ctrl+Shift+D) opens the one you added last, in any book."),
          tr("<b>The Notebook</b> (%1, Ctrl+Shift+N) lists the book's highlights, notes and bookmarks; export them as "
             "Markdown, plain text, CSV, TSV or JSON.").arg(link(QStringLiteral("notebook"), tr("Show Notebook"))),
          tr("<b>Search:</b> %1 (Ctrl+F) or %2 (Ctrl+Shift+F). Accents and capitals don't matter.")
              .arg(link(QStringLiteral("search-book"), tr("Search This Book")), link(QStringLiteral("search-library"), tr("Search All Books"))),
          tr("<b>Export and import:</b> the Notebook's <b>Export…</b> saves Markdown, plain text, CSV, TSV or JSON; %1 reads "
             "any of them back, or a Kindle's highlights.")
              .arg(link(QStringLiteral("import-kindle"), tr("Import Highlights…")))});

    section("practice");
    list({tr("Every word you look up or highlight is saved in the <b>Word List</b>, with its meaning, book and page."),
          tr("<b>While reading:</b> the <b>Practice</b> button in the header (Ctrl+P) opens flashcards and the words of "
             "this page beside the book."),
          tr("<b>In the library:</b> %1 opens Practice in the whole window.").arg(link(QStringLiteral("practice"), tr("Practice Words"))),
          tr("Choose the directions (the book's language first, yours first, definition, a sentence with a gap), flip the "
             "card or type the answer, and the order."),
          tr("<b>Word List:</b> %1: search, sort, change a meaning, Show in Book, and <b>Export…</b> for Anki.")
              .arg(link(QStringLiteral("word-list"), tr("Open the Word List"))),
          tr("<b>Statistics:</b> %1: time read, days in a row, words and reviews.").arg(link(QStringLiteral("statistics"), tr("Open Statistics")))});

    section("library");
    list({tr("Filter by <b>Reading</b>, <b>Finished</b>, <b>Not Started</b>, by language, or by your collections "
             "(<b>+ New Collection</b>, then drag books onto it)."),
          tr("Search by title or author; sort; switch between covers and a list. Ctrl+wheel makes covers bigger or smaller."),
          tr("<b>Continue Reading</b> at the bottom reopens the last book."),
          tr("A book whose file moved shows <b>File Not Found</b>: right-click it, <b>Locate File…</b>.")});

    section("look");
    list({tr("<b>Text and layout:</b> the <b>Aa</b> button (%1): font, text size, spacing, margins, page width, "
             "columns, page turn animations. <b>Apply To</b> chooses all books, one language, or this book.")
              .arg(link(QStringLiteral("appearance"), tr("Open Text and Layout"))),
          tr("<b>Quick keys:</b> Ctrl+= and Ctrl+- change the text size, Ctrl+] and Ctrl+[ the margins."),
          tr("<b>Themes:</b> 43 themes restyle the whole app. %1 (Ctrl+Shift+T); point at one to preview it.")
              .arg(link(QStringLiteral("themes"), tr("Choose Theme…")))});

    section("audio");
    list({tr("<b>Hear Pronunciation:</b> the speaker button on a word's card and in the translation pop-up."),
          tr("<b>Read Aloud:</b> right-click a selection; <b>Stop Reading Aloud</b> in the same menu stops it."),
          tr("<b>Speed, volume and voices:</b> %1. Each language can have its own voice; <b>Play Sample</b> lets you hear it.")
              .arg(link(QStringLiteral("audio-settings"), tr("Audio Settings"))),
          tr("No voice for the book's language? On Windows, add one (free) in Settings ▸ Time &amp; Language ▸ Speech.")});

    section("settings");
    list({tr("%1: your language (FreeReader's text, translations and explanations), the language packs (a download per language read: its "
             "dictionary, its definitions and the offline translator), the app data folder, the library's folders, resetting "
             "text and layout.").arg(link(QStringLiteral("settings"), tr("General"))),
          tr("%1: the Gemini key, LM Studio, offline dictionaries and the offline translator.")
              .arg(link(QStringLiteral("translation-settings"), tr("Translation"))),
          tr("%1: speed, volume and the voice for each language.").arg(link(QStringLiteral("audio-settings"), tr("Audio"))),
          tr("%1: every shortcut can be changed, and mouse buttons can run commands.")
              .arg(link(QStringLiteral("keyboard-shortcuts"), tr("Keyboard Shortcuts"))),
          tr("Changes show at once. <b>OK</b> keeps them; <b>Cancel</b> puts back what you changed.")});

    section("faq");
    auto qa = [&](const QString& q, const QString& a) { h += QStringLiteral("<p><b>%1</b><br>%2</p>").arg(q, a); };
    qa(tr("Do I need to pay for translations?"),
       tr("No. Gemini's free tier needs no card. Without a key, LM Studio, the offline dictionaries and the offline "
          "translator still answer."));
    qa(tr("Does it work without internet?"),
       tr("Yes, with a language's pack: its dictionary, its definitions and the offline translator (Settings > General). "
          "Answers looked up before are kept and work offline too."));
    qa(tr("Which languages can I read?"),
       tr("English and Spanish, each from the other, with everything offline as well; and books in your own language, "
          "with its definitions. FreeReader itself is in English or Spanish. More languages come as their packs are made "
          "complete: every language offered has its dictionary, its definitions and the offline translator."));
    qa(tr("Is my text sent anywhere?"),
       tr("Only to Gemini, and only the words and sentence you ask about. Turn off Use Gemini, or keep one book offline, "
          "to send nothing."));
    qa(tr("Why doesn't Hear Pronunciation say anything?"),
       tr("No voice for the book's language is installed. The pop-up says so, with a link to add one; Audio Settings "
          "lists the voices found."));
    qa(tr("Does FreeReader change my book files?"),
       tr("Never. Highlights, notes and your place are kept in FreeReader's own database."));
    qa(tr("Can it open books with DRM?"),
       tr("No. FreeReader opens DRM-free books only (EPUB, PDF, Kindle MOBI and AZW3, comics, documents and more)."));
    qa(tr("Where are my notes and settings kept?"),
       tr("In the app data folder, shown in %1. Move it there too.").arg(link(QStringLiteral("settings"), tr("General Settings"))));
    qa(tr("How do I get my words into Anki?"),
       tr("Word List, then Export…: Anki imports the file."));
    return h;
}

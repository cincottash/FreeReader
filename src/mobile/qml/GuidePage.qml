// Getting Started: how the phone app works, section by section (the desktop's guide, for fingers instead of a mouse).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Page {
    id: guide
    property MobileApp app
    property Theme theme
    signal closeRequested()

    background: Rectangle { color: guide.theme.bg }
    header: Rectangle {
        height: 56
        color: guide.theme.surface
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            BarButton { text: qsTr("‹ Back"); theme: guide.theme; onClicked: guide.closeRequested() }
            Label { text: qsTr("Getting Started"); font.pixelSize: guide.theme.title; font.weight: Font.DemiBold; color: guide.theme.fg; Layout.fillWidth: true }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: guide.theme.line }
    }

    // {title, lines}: each section of the guide.
    readonly property var sections: [
        { title: qsTr("Your Books"), lines: [
            qsTr("Tap Add Book and choose a book on this phone: EPUB, Kindle (MOBI, AZW3), FB2, comics (CBZ), text and more. Books with DRM can't be opened."),
            qsTr("Tap a book to read it; press and hold it for its menu: Collections, Mark as Finished, Remove from Library."),
            qsTr("Above the books: Search Title or Author, the filter (Reading, Finished, a language, a collection) and the order. The icon beside the search shows covers or a list."),
            qsTr("Continue Reading opens the book you read last where you left it.") ] },
        { title: qsTr("Reading"), lines: [
            qsTr("Swipe left or right to turn the page. A tap never turns it: a tap opens the menu (and a tap on a link follows it; on a picture, shows it full screen)."),
            qsTr("The menu: Contents and Notebook, Search, Bookmark This Page, and Aa for the text and layout. At the bottom: the chapter, Go to Page or Percent, the previous and next chapter."),
            qsTr("After a jump (contents, a search result, a link) the chip Back to Page… takes you back."),
            qsTr("Aa: the font, text size, line spacing, margins, page width, two pages side by side on a wide screen, the page-turn animation and the theme, for all books, the book's language or this book only.") ] },
        { title: qsTr("Words and Sentences"), lines: [
            qsTr("Press and hold a word to select it; keep holding and drag to take in more words. Lift your finger to see what to do with them."),
            qsTr("Dictionary: the word's card from the offline dictionaries (Flip for the other language). Translate: the meaning in this sentence. Explain: the whole sentence, with notes on its grammar and idioms."),
            qsTr("Listen reads the words aloud; the speaker on a card says the word."),
            qsTr("Highlight in a color, add a note, or bookmark the word. Everything is in the Notebook (Contents and Notebook > Notebook).") ] },
        { title: qsTr("Translation"), lines: [
            qsTr("Gemini (Google, online) answers in context; add a free key in Settings. The offline dictionaries work without the internet. Your Word List is asked last."),
            qsTr("In Settings: use Gemini only for Explain This Sentence, how long to wait for it, and Keep This Book Offline."),
            qsTr("Show %1 Alongside (the menu's bottom bar) shows each paragraph of the page in your language, under the page or beside it on a tablet.").arg(guide.app.alongsideText.title || "") ] },
        { title: qsTr("Practice"), lines: [
            qsTr("Every word you translate or highlight joins the Word List (Words, in the library). Practice shows them as flashcards, with the same schedule as the desktop."),
            qsTr("Choose which words (from a book, starred), which kind of card, whether to type the answer, and the order on the start screen."),
            qsTr("Tap a word in the Word List for Show in Book, Change Meaning or Start Over.") ] },
        { title: qsTr("Your Notes"), lines: [
            qsTr("Notebook > Export… shares the Notebook (to email, Drive, Keep…) or saves it as Markdown, text, CSV, TSV or JSON."),
            qsTr("Notebook > Import Highlights… brings in a Kindle export (Bookcision's JSON or My Clippings.txt) or a Biblioglot Notebook export. What would come in is shown first."),
            qsTr("Statistics (the library's menu) shows your reading time, days in a row, new words and reviews.") ] },
        { title: qsTr("Privacy"), lines: [
            qsTr("Your books, notes and words stay on this phone. Only the words you translate with Gemini are sent to Google, with your key."),
            qsTr("Report a problem with an AI answer with Report on its card.") ] }
    ]

    Flickable {
        objectName: "guideFlick"
        anchors.fill: parent
        contentHeight: body.implicitHeight + 40
        clip: true
        ColumnLayout {
            id: body
            x: 20
            y: 12
            width: parent.width - 40
            spacing: 6
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: guide.theme.body
                color: guide.theme.fg
                text: qsTr("Biblioglot is an e-book reader built for reading in another language: press and hold a word and its meaning in this sentence is a tap away; the words you look up become flashcards.")
            }
            Repeater {
                model: guide.sections
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        Layout.topMargin: 14
                        text: modelData.title.toUpperCase()
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                        color: guide.theme.muted
                    }
                    Repeater {
                        model: modelData.lines
                        delegate: Label {
                            required property string modelData
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: "• " + modelData
                            font.pixelSize: guide.theme.body
                            color: guide.theme.fg
                        }
                    }
                }
            }
        }
    }
}

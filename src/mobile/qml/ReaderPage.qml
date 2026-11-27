// Reading: swipe to turn the page (a tap never turns it: the owner kept turning pages by tapping a word, 2026-11-27);
// tap for the menu (Library, Contents and Notebook, Search, Bookmark, text and theme). Press and hold a word, and keep
// holding and drag to take in more words; lifting the finger shows what to do with them: highlight, note,
// Dictionary, Translate, Copy.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Page {
    id: reader
    property MobileApp app
    property Theme theme
    property bool chrome: false
    property bool selecting: false
    property real pressY: 0
    signal settingsRequested()
    signal closeRequested()

    function showChrome(on) { chrome = on }
    // The screenshot mode opens these by name.
    function showPart(name) {
        if (name === "contents") { nav.tab = 0; nav.open() }
        else if (name === "notebook") { nav.tab = 1; nav.open() }
        else if (name === "search") search.open()
        else if (name === "look") { chrome = true; look.open() }
        else if (name === "selection") { selectionBar.show(book.height * 0.3) }
        else if (name === "dictionary") { if (reader.app.showDictionary(book)) card.open() }
    }
    function lookUpSelection() { reader.app.lookUp(book) }

    background: Rectangle { color: reader.theme.bg }
    StackView.onRemoved: app.closeBook()

    BookPage {
        id: book
        objectName: "bookPage"
        anchors.fill: parent
        anchors.bottomMargin: footer.height
        app: reader.app
        steady: reader.selecting
        onPositionChanged: reader.app.pageMoved(book)
        onReadyChanged: reader.app.pageMoved(book)

        TapHandler {
            enabled: !reader.selecting
            onTapped: (point) => {
                if (sheet.opened) { sheet.close(); return }
                if (card.opened) { card.close(); return }
                if (book.hasSelection) { book.clearSelection(); return }
                // Anywhere on the page: the menu. Pages turn only by swiping.
                reader.chrome = !reader.chrome
            }
            onLongPressed: {
                if (book.selectWordAt(point.position.x, point.position.y)) {
                    reader.selecting = true
                    reader.chrome = false
                    reader.pressY = point.position.y
                }
            }
        }
        // Swipes turn pages; after a long press, dragging takes in more words.
        DragHandler {
            id: drag
            target: null
            xAxis.enabled: true
            yAxis.enabled: reader.selecting
            property real swipeX: 0   // how far the finger moved sideways, while it is down
            onCentroidChanged: {
                if (!active) return
                swipeX = centroid.position.x - centroid.pressPosition.x
                if (reader.selecting) book.extendSelectionTo(centroid.position.x, centroid.position.y)
            }
            onActiveChanged: {
                if (active) { swipeX = 0; return }
                if (reader.selecting) return
                // Right to left, a swipe to the right goes forward.
                const forward = reader.app.rightToLeft ? swipeX > 40 : swipeX < -40
                const back = reader.app.rightToLeft ? swipeX < -40 : swipeX > 40
                if (forward) book.nextPage()
                else if (back) book.previousPage()
            }
        }
        // Lifting the finger after a long press shows what to do with the words.
        PointHandler {
            onActiveChanged: if (!active && reader.selecting) {
                reader.selecting = false
                if (book.hasSelection) selectionBar.show(reader.pressY)
            }
        }
    }

    // Page and chapter, always shown at the bottom.
    Rectangle {
        id: footer
        anchors.bottom: parent.bottom
        width: parent.width
        height: 28
        color: reader.theme.bg
        // "Page 7 of 1,180 · 6 min left in chapter": a tap on it switches to the pages left (and back).
        Label {
            objectName: "footerText"
            anchors.centerIn: parent
            text: reader.app.pageText + (reader.app.chapterLeft.length > 0 ? "   ·   " + reader.app.chapterLeft : "")
            font.pixelSize: 12
            color: reader.theme.muted
            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                onClicked: reader.app.toggleChapterLeft()
            }
        }
    }

    // The menu: tap the middle of the page.
    Rectangle {
        id: topBar
        visible: reader.chrome
        width: parent.width
        height: 56
        color: reader.theme.surface
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 2
            anchors.rightMargin: 2
            spacing: 0
            IconButton { theme: reader.theme; iconName: "back"; text: qsTr("Library"); onClicked: reader.closeRequested() }
            Label {
                Layout.fillWidth: true
                text: reader.app.bookTitle
                elide: Text.ElideRight
                font.pixelSize: reader.theme.body
                font.weight: Font.DemiBold
                color: reader.theme.fg
            }
            IconButton { objectName: "contentsButton"; theme: reader.theme; iconName: "toc"; text: qsTr("Contents and Notebook"); onClicked: { nav.tab = 0; nav.open() } }
            IconButton { objectName: "searchButton"; theme: reader.theme; iconName: "search"; text: qsTr("Search This Book"); onClicked: search.open() }
            IconButton {
                objectName: "bookmarkButton"
                theme: reader.theme
                iconName: "bookmark"
                text: reader.app.pageBookmarked ? qsTr("Remove the Bookmark") : qsTr("Bookmark This Page")
                filled: reader.app.pageBookmarked
                iconColor: reader.app.pageBookmarked ? reader.theme.accent : reader.theme.fg
                onClicked: reader.app.toggleBookmark()
            }
            BarButton {
                objectName: "lookButton"
                text: "Aa"
                theme: reader.theme
                textColor: reader.theme.fg
                font.pixelSize: 18
                font.family: reader.theme.serif
                onClicked: look.opened ? look.close() : look.open()
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: reader.theme.line }
    }
    Rectangle {
        id: bottomBar
        visible: reader.chrome
        anchors.bottom: footer.top
        width: parent.width
        height: 64
        color: reader.theme.surface
        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Label {
                width: parent.width
                text: reader.app.chapterTitle
                elide: Text.ElideRight
                font.pixelSize: reader.theme.small
                color: reader.theme.fg
            }
            Rectangle {
                width: parent.width
                height: 4
                radius: 2
                color: reader.theme.line
                Rectangle { width: parent.width * reader.app.progress; height: parent.height; radius: 2; color: reader.theme.accent }
            }
        }
    }

    // Text size, margins and theme.
    Popup {
        id: look
        x: parent.width - width - 8
        y: topBar.height + 4
        width: Math.min(360, parent.width - 16)
        height: Math.min(parent.height - topBar.height - 60, 620)
        padding: 16
        background: Rectangle { color: reader.theme.pop; radius: 10; border.color: reader.theme.line }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label { text: qsTr("Text Size"); color: reader.theme.muted; font.pixelSize: reader.theme.small }
            RowLayout {
                Button { text: "A−"; onClicked: reader.app.textSize = reader.app.textSize - 10 }
                Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: reader.app.textSize + " %"; color: reader.theme.fg }
                Button { text: "A+"; onClicked: reader.app.textSize = reader.app.textSize + 10 }
            }
            Label { text: qsTr("Margins"); color: reader.theme.muted; font.pixelSize: reader.theme.small }
            RowLayout {
                Button { text: qsTr("Narrower"); onClicked: reader.app.margins = reader.app.margins - 8 }
                Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: reader.app.margins + " px"; color: reader.theme.fg }
                Button { text: qsTr("Wider"); onClicked: reader.app.margins = reader.app.margins + 8 }
            }
            Label { text: qsTr("Theme"); color: reader.theme.muted; font.pixelSize: reader.theme.small }
            ThemeList {
                objectName: "themeList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                app: reader.app
                theme: reader.theme
            }
            Button {
                text: qsTr("Settings…")
                flat: true
                onClicked: { look.close(); reader.settingsRequested() }
            }
        }
    }

    SelectionBar {
        id: selectionBar
        objectName: "selectionBar"
        app: reader.app
        theme: reader.theme
        page: book
        onNoteRequested: note.edit()
        onDictionaryRequested: {
            // No dictionary knows the word: the translation sheet, as on the desktop.
            if (reader.app.showDictionary(book)) card.open()
            else reader.lookUpSelection()
        }
        onTranslateRequested: reader.lookUpSelection()
    }

    NoteEditor { id: note; app: reader.app; theme: reader.theme; page: book; onClosed: book.clearSelection() }
    Navigator { id: nav; app: reader.app; theme: reader.theme }
    SearchSheet { id: search; app: reader.app; theme: reader.theme }

    Connections {
        target: reader.app
        function onLookupChanged() { if ((reader.app.lookup.text || "").length > 0 && !sheet.opened) sheet.open() }
    }

    LookupSheet {
        id: sheet
        app: reader.app
        theme: reader.theme
        page: book
        onClosed: book.clearSelection()
    }
    DictionarySheet {
        id: card
        objectName: "dictionarySheet"
        app: reader.app
        theme: reader.theme
        onTranslateRequested: reader.lookUpSelection()
        onSettingsRequested: reader.settingsRequested()
        onClosed: if (!sheet.opened) book.clearSelection()
    }
}

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
    // Closing this screen closes its own book only (Show in Book can open another one meanwhile).
    property int openedId: 0
    Component.onCompleted: openedId = app.bookId
    StackView.onRemoved: if (app.bookId === openedId) app.closeBook()

    // A tablet (or a phone on its side): Show English Alongside goes beside the page, else under it.
    readonly property bool wide: width >= 760

    BookPage {
        id: book
        objectName: "bookPage"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: alongsidePanel.visible && reader.wide ? alongsidePanel.left : parent.right
        anchors.bottom: alongsidePanel.visible && !reader.wide ? alongsidePanel.top : footer.top
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
                // A link (a footnote, another chapter, a web page) or a picture.
                if (book.tapAt(point.position.x, point.position.y)) return
                // Anywhere else on the page: the menu. Pages turn only by swiping.
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

    // Show English Alongside: each paragraph of the screen in the reader's language, in the same order.
    Rectangle {
        id: alongsidePanel
        objectName: "alongsidePanel"
        visible: reader.app.alongside
        color: reader.theme.surface
        anchors.right: parent.right
        anchors.bottom: footer.top
        width: reader.wide ? Math.min(380, parent.width * 0.4) : parent.width
        height: reader.wide ? parent.height - footer.height : Math.round(parent.height * 0.4)
        readonly property var data_: reader.app.alongsideText
        Rectangle { width: reader.wide ? 1 : parent.width; height: reader.wide ? parent.height : 1; color: reader.theme.line }
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 4
            anchors.topMargin: 4
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: (alongsidePanel.data_.title || "").toUpperCase()
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                    color: reader.theme.muted
                }
                IconButton { objectName: "alongsideClose"; theme: reader.theme; iconName: "close"; iconColor: reader.theme.muted; text: qsTr("Close"); onClicked: reader.app.alongside = false }
            }
            Label {
                objectName: "alongsideStatus"
                Layout.fillWidth: true
                Layout.rightMargin: 12
                visible: text.length > 0
                text: alongsidePanel.data_.status || ""
                wrapMode: Text.Wrap
                font.pixelSize: reader.theme.small
                color: reader.theme.muted
            }
            ListView {
                id: alongsideList
                objectName: "alongsideList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 10
                model: alongsidePanel.data_.paragraphs || []
                delegate: Label {
                    required property var modelData
                    width: alongsideList.width - 12
                    visible: modelData.english.length > 0 || alongsidePanel.data_.waiting === true
                    height: visible ? implicitHeight : 0
                    text: modelData.english.length > 0 ? modelData.english : "…"
                    wrapMode: Text.Wrap
                    font.family: reader.theme.serif
                    font.pixelSize: reader.theme.body
                    color: modelData.english.length > 0 ? reader.theme.fg : reader.theme.muted
                }
                footer: Item { width: 1; height: 12 }
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
    // The chapter, how far into the book, and moving around: chapters, the beginning, a page or a percentage.
    Rectangle {
        id: bottomBar
        objectName: "bottomBar"
        visible: reader.chrome
        anchors.bottom: footer.top
        width: parent.width
        height: navColumn.implicitHeight + 16
        color: reader.theme.surface
        Rectangle { width: parent.width; height: 1; color: reader.theme.line }
        ColumnLayout {
            id: navColumn
            x: 12
            y: 10
            width: parent.width - 24
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: reader.app.chapterTitle
                elide: Text.ElideRight
                font.pixelSize: reader.theme.small
                color: reader.theme.fg
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 4
                height: 4
                radius: 2
                color: reader.theme.line
                Rectangle { width: parent.width * reader.app.progress; height: parent.height; radius: 2; color: reader.theme.accent }
            }
            Button {
                objectName: "alongsideButton"
                Layout.fillWidth: true
                Layout.topMargin: 4
                flat: true
                font.pixelSize: reader.theme.small
                readonly property string language: reader.app.alongsideText.title || ""
                text: reader.app.alongside ? qsTr("Hide %1 Alongside").arg(language) : qsTr("Show %1 Alongside").arg(language)
                onClicked: reader.app.alongside = !reader.app.alongside
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 0
                IconButton { objectName: "beginningButton"; theme: reader.theme; iconName: "beginning"; text: qsTr("Go to Beginning of Book"); onClicked: reader.app.goToBeginning() }
                IconButton { objectName: "previousChapterButton"; theme: reader.theme; iconName: "chapterPrevious"; text: qsTr("Previous Chapter"); onClicked: reader.app.previousChapter() }
                Button {
                    objectName: "goToPageButton"
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    Layout.rightMargin: 4
                    text: qsTr("Go to Page or Percent")
                    flat: true
                    font.pixelSize: reader.theme.small
                    onClicked: goTo.open()
                }
                IconButton { objectName: "nextChapterButton"; theme: reader.theme; iconName: "chapterNext"; text: qsTr("Next Chapter"); onClicked: reader.app.nextChapter() }
            }
        }
    }

    // After a jump: back to where the reader was (as on the desktop).
    Rectangle {
        id: backChip
        objectName: "backChip"
        z: 5
        visible: reader.app.backText.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: reader.chrome ? bottomBar.top : book.bottom
        anchors.bottomMargin: 6
        width: backRow.implicitWidth + 28
        height: 36
        radius: 18
        color: reader.theme.pop
        border.color: reader.theme.line
        RowLayout {
            id: backRow
            anchors.centerIn: parent
            spacing: 6
            Icon { width: 18; height: 18; name: "undo"; color: reader.theme.accent }
            Label { text: reader.app.backText; font.pixelSize: reader.theme.small; color: reader.theme.fg }
        }
        MouseArea { anchors.fill: parent; onClicked: reader.app.goBack() }
    }

    // Go to Page or Percent.
    Dialog {
        id: goTo
        objectName: "goToDialog"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 380)
        modal: true
        title: qsTr("Go to Page or Percent")
        onOpened: { goToField.text = ""; goToError.text = ""; goToField.placeholderText = reader.app.pageOrPercentHint(); goToField.forceActiveFocus() }
        function go() {
            const problem = reader.app.goToPageOrPercent(goToField.text)
            if (problem.length > 0) goToError.text = problem
            else { goTo.close(); reader.chrome = false }
        }
        ColumnLayout {
            width: parent.width
            spacing: 8
            TextField {
                id: goToField
                objectName: "goToField"
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
                color: reader.theme.fg
                onAccepted: goTo.go()
            }
            Label {
                id: goToError
                objectName: "goToError"
                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.Wrap
                font.pixelSize: reader.theme.small
                color: reader.theme.accent
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Cancel"); flat: true; onClicked: goTo.close() }
            Button { objectName: "goToGo"; text: qsTr("Go"); highlighted: true; Layout.rightMargin: 12; Layout.bottomMargin: 12; onClicked: goTo.go() }
        }
    }

    // A footnote's text, over the page; Go to the Note jumps to it (Back returns).
    Dialog {
        id: footnote
        objectName: "footnoteDialog"
        anchors.centerIn: parent
        width: parent.width - 32
        modal: true
        title: qsTr("Footnote")
        property alias text: footnoteText.text
        ScrollView {
            width: parent.width
            implicitHeight: Math.min(footnoteText.implicitHeight, reader.height * 0.5)
            clip: true
            Label {
                id: footnoteText
                objectName: "footnoteText"
                width: footnote.availableWidth
                wrapMode: Text.Wrap
                font.family: reader.theme.serif
                font.pixelSize: reader.theme.body
                color: reader.theme.fg
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button { objectName: "footnoteGo"; text: qsTr("Go to the Note"); flat: true; onClicked: { footnote.close(); reader.app.goToFootnote() } }
            Button { text: qsTr("Close"); highlighted: true; Layout.rightMargin: 12; Layout.bottomMargin: 12; onClicked: footnote.close() }
        }
    }

    // A picture from the book, full screen: pinch to zoom, drag to look around, double-tap for its whole size again.
    Popup {
        id: picture
        objectName: "pictureViewer"
        parent: Overlay.overlay
        x: 0
        y: 0
        width: parent ? parent.width : 400
        height: parent ? parent.height : 800
        padding: 0
        modal: true
        onClosed: reader.app.closeImage()
        Connections {
            target: reader.app
            function onImageChanged() {
                if (reader.app.shownImage.length > 0) { shown.scale = 1; shown.x = 0; shown.y = 0; picture.open() }
                else picture.close()
            }
        }
        background: Rectangle { color: "#000000" }
        Item {
            anchors.fill: parent
            clip: true
            Image {
                id: shown
                width: parent.width
                height: parent.height
                source: reader.app.shownImage
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
                PinchHandler { id: pinch; target: shown; scaleAxis.minimum: 1; scaleAxis.maximum: 6; rotationAxis.enabled: false }
                DragHandler { target: shown; enabled: shown.scale > 1.01 }
                TapHandler {
                    onDoubleTapped: { shown.scale = 1; shown.x = 0; shown.y = 0 }
                }
            }
            IconButton {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: reader.app.safeTop + 4
                anchors.rightMargin: 4
                theme: reader.theme
                iconName: "close"
                iconColor: "#ffffff"
                text: qsTr("Close")
                onClicked: picture.close()
            }
        }
    }

    // Text and Layout: the font, size, spacing, margins, page width, pages side by side, page turns and the theme.
    LookPanel {
        id: look
        objectName: "lookPanel"
        app: reader.app
        theme: reader.theme
        x: parent.width - width - 8
        y: topBar.height + 4
        width: Math.min(380, parent.width - 16)
        height: Math.min(parent.height - topBar.height - 60, 720)
        onSettingsRequested: reader.settingsRequested()
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
        onExplainRequested: reader.app.explain(book)   // Explain This Sentence, in the same sheet
    }

    NoteEditor { id: note; app: reader.app; theme: reader.theme; page: book; onClosed: book.clearSelection() }
    Navigator {
        id: nav
        app: reader.app
        theme: reader.theme
        onExportRequested: { nav.close(); notebookTools.exportNotebook() }
        onImportRequested: { nav.close(); notebookTools.importHighlights() }
    }
    NotebookTools { id: notebookTools; app: reader.app; theme: reader.theme }
    SearchSheet { id: search; app: reader.app; theme: reader.theme }

    Connections {
        target: reader.app
        function onLookupChanged() { if ((reader.app.lookup.text || "").length > 0 && !sheet.opened) sheet.open() }
        function onBookmarkQuestion(question, count) { bookmarkDialog.ask(question, count) }
        function onFootnoteShown(text) { footnote.text = text; footnote.open() }
    }

    // The app's messages while reading (a missing voice for Listen, no bookmarks yet), as the library shows them.
    Rectangle {
        id: toast
        objectName: "readerMessage"
        z: 10
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        anchors.bottomMargin: footer.height + 16
        height: toastText.implicitHeight + 24
        radius: 8
        color: reader.theme.pop
        border.color: reader.theme.line
        visible: toastText.text.length > 0 && toastTimer.running
        Label {
            id: toastText
            anchors.fill: parent
            anchors.margins: 12
            text: reader.app.message
            wrapMode: Text.Wrap
            font.pixelSize: reader.theme.small
            color: reader.theme.fg
        }
        Timer { id: toastTimer; interval: 6000 }
        Connections {
            target: reader.app
            function onMessageChanged() { if (reader.app.message.length > 0) toastTimer.restart() }
        }
    }

    // A new bookmark where the book has others: remove the earlier one(s)? (as on the desktop)
    Dialog {
        id: bookmarkDialog
        objectName: "bookmarkDialog"
        property int count: 0
        property alias question: questionLabel.text
        function ask(q, n) { question = q; count = n; open() }
        anchors.centerIn: parent
        width: parent.width - 48
        modal: true
        title: qsTr("Bookmark Added")
        Label { id: questionLabel; width: parent.width; wrapMode: Text.Wrap; color: reader.theme.fg }
        footer: ColumnLayout {
            spacing: 4
            Button {
                objectName: "bookmarkRemovePrevious"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                text: qsTr("Remove Previous Bookmark")
                highlighted: true
                onClicked: { reader.app.removeEarlierBookmarks(false); bookmarkDialog.close() }
            }
            Button {
                objectName: "bookmarkRemoveAll"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                visible: bookmarkDialog.count > 1
                text: qsTr("Remove All %1").arg(bookmarkDialog.count)
                onClicked: { reader.app.removeEarlierBookmarks(true); bookmarkDialog.close() }
            }
            Button {
                objectName: "bookmarkKeep"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                text: bookmarkDialog.count === 1 ? qsTr("Keep Both") : qsTr("Keep All")
                onClicked: bookmarkDialog.close()
            }
        }
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

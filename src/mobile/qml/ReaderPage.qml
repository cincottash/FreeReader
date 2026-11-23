// Reading: tap the left third for the previous page and the right third for the next one, or swipe; tap the middle
// for the menu (back to the library, text size, margins, theme). Press and hold a word to look it up; keep holding
// and drag to take in more words.
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
    signal settingsRequested()
    signal closeRequested()

    function showChrome(on) { chrome = on }

    background: Rectangle { color: reader.theme.bg }
    StackView.onRemoved: app.closeBook()

    BookPage {
        id: book
        anchors.fill: parent
        anchors.bottomMargin: footer.height
        app: reader.app
        onPositionChanged: reader.app.pageMoved(book)
        onReadyChanged: reader.app.pageMoved(book)

        TapHandler {
            enabled: !reader.selecting
            onTapped: (point) => {
                if (sheet.opened) { sheet.close(); return }
                if (book.hasSelection) { book.clearSelection(); return }
                const x = point.position.x
                if (x < book.width / 3) book.previousPage()
                else if (x > book.width * 2 / 3) book.nextPage()
                else reader.chrome = !reader.chrome
            }
            onLongPressed: {
                if (book.selectWordAt(point.position.x, point.position.y)) {
                    reader.selecting = true
                    reader.chrome = false
                }
            }
        }
        // Swipes turn pages; after a long press, dragging takes in more words.
        DragHandler {
            id: drag
            target: null
            xAxis.enabled: true
            yAxis.enabled: reader.selecting
            onCentroidChanged: if (reader.selecting && active) book.extendSelectionTo(centroid.position.x, centroid.position.y)
            onActiveChanged: {
                if (active) return
                if (reader.selecting) return
                if (translation.x < -40) book.nextPage()
                else if (translation.x > 40) book.previousPage()
            }
        }
        // Lifting the finger after a long press looks the selection up.
        PointHandler {
            onActiveChanged: if (!active && reader.selecting) {
                reader.selecting = false
                if (book.hasSelection) reader.app.lookUp(book)
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
        Label {
            anchors.centerIn: parent
            text: reader.app.pageText
            font.pixelSize: 12
            color: reader.theme.muted
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
            anchors.leftMargin: 4
            anchors.rightMargin: 4
            BarButton {
                text: qsTr("‹ Library")
                theme: reader.theme
                onClicked: reader.closeRequested()
            }
            Label {
                Layout.fillWidth: true
                text: reader.app.bookTitle
                elide: Text.ElideRight
                font.pixelSize: reader.theme.body
                font.weight: Font.DemiBold
                color: reader.theme.fg
                horizontalAlignment: Text.AlignHCenter
            }
            BarButton {
                text: "Aa"
                theme: reader.theme
                textColor: reader.theme.fg
                font.pixelSize: 18
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
        width: Math.min(340, parent.width - 16)
        padding: 16
        background: Rectangle { color: reader.theme.pop; radius: 10; border.color: reader.theme.line }
        ColumnLayout {
            width: parent.width
            spacing: 12
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
            RowLayout {
                spacing: 8
                Repeater {
                    model: [["paper", qsTr("Paper")], ["sepia", qsTr("Sepia")], ["night", qsTr("Night")]]
                    delegate: Button {
                        required property var modelData
                        text: modelData[1]
                        checkable: true
                        checked: reader.app.theme === modelData[0]
                        onClicked: reader.app.theme = modelData[0]
                    }
                }
            }
            Button {
                text: qsTr("Settings…")
                flat: true
                onClicked: { look.close(); reader.settingsRequested() }
            }
        }
    }

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
}

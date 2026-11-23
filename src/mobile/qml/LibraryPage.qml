// The library: the books on this phone, most recently read first. Add Book picks an EPUB file; tap a book to read
// it; press and hold for Remove.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FreeReaderMobile

Page {
    id: page
    property MobileApp app
    property Theme theme
    signal settingsRequested()

    background: Rectangle { color: page.theme.bg }

    header: Rectangle {
        height: 56
        color: page.theme.surface
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 4
            Label {
                text: "FreeReader"
                font.pixelSize: page.theme.title
                font.weight: Font.DemiBold
                color: page.theme.fg
                Layout.fillWidth: true
            }
            ToolButton {
                id: settingsButton
                text: qsTr("Settings")
                font.pixelSize: page.theme.body
                implicitHeight: page.theme.touch
                contentItem: Label { text: settingsButton.text; color: page.theme.accent; font: settingsButton.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Item {}
                onClicked: page.settingsRequested()
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: page.theme.line }
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.margins: 12
        clip: true
        model: page.app.books
        readonly property int columns: Math.max(2, Math.floor(width / 150))
        cellWidth: Math.floor(width / columns)
        cellHeight: cellWidth * 1.5 + 64
        visible: page.app.bookCount > 0
        delegate: Item {
            id: tile
            required property int bookId
            required property string title
            required property string author
            required property double progress
            required property bool hasCover
            width: grid.cellWidth
            height: grid.cellHeight
            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                Rectangle {
                    width: parent.width
                    height: width * 1.5
                    radius: 4
                    color: Qt.hsla((tile.title.length * 37 % 360) / 360, 0.35, page.theme.dark ? 0.3 : 0.7, 1)
                    border.color: page.theme.line
                    clip: true
                    Image {
                        anchors.fill: parent
                        visible: tile.hasCover
                        source: tile.hasCover ? "image://covers/" + tile.bookId : ""
                        fillMode: Image.PreserveAspectFit
                        sourceSize.height: 400
                    }
                    Label {
                        visible: !tile.hasCover
                        anchors.fill: parent
                        anchors.margins: 12
                        text: tile.title
                        wrapMode: Text.Wrap
                        font.family: page.theme.serif
                        font.pixelSize: 16
                        color: page.theme.fg
                    }
                }
                Label {
                    width: parent.width
                    text: tile.title
                    elide: Text.ElideRight
                    font.pixelSize: page.theme.small
                    font.weight: Font.DemiBold
                    color: page.theme.fg
                }
                Label {
                    width: parent.width
                    text: tile.progress > 0.0005 ? Math.round(tile.progress * 100) + " %  ·  " + tile.author : tile.author
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: page.theme.muted
                }
            }
            TapHandler {
                onTapped: page.app.openBook(tile.bookId)
                onLongPressed: {
                    bookMenu.bookId = tile.bookId
                    bookMenu.bookTitle = tile.title
                    bookMenu.popup()
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 64
        visible: page.app.bookCount === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: qsTr("No books yet.\nTap Add Book to choose an EPUB file on this phone. Books with DRM can't be opened.")
        font.pixelSize: page.theme.body
        color: page.theme.muted
    }

    Menu {
        id: bookMenu
        property int bookId: 0
        property string bookTitle: ""
        MenuItem {
            text: qsTr("Remove from Library")
            onTriggered: removeDialog.open()
        }
    }
    Dialog {
        id: removeDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Remove “%1”?").arg(bookMenu.bookTitle)
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label { text: qsTr("The book and its place are removed from this phone. Its words stay in the Word List."); wrapMode: Text.Wrap; width: page.width - 96 }
        onAccepted: page.app.removeBook(bookMenu.bookId)
    }

    FileDialog {
        id: picker
        title: qsTr("Add Book")
        nameFilters: [qsTr("EPUB books (*.epub)")]
        onAccepted: page.app.importBook(selectedFile)
    }

    Button {
        id: addButton
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        text: qsTr("Add Book")
        enabled: !page.app.busy
        font.pixelSize: page.theme.body
        font.weight: Font.DemiBold
        implicitHeight: 52
        leftPadding: 24
        rightPadding: 24
        contentItem: Label { text: addButton.text; font: addButton.font; color: page.theme.accentText; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 26; color: page.theme.accent; opacity: addButton.enabled ? 1 : 0.5 }
        onClicked: picker.open()
    }

    // Messages ("Added …", why a book couldn't be added) for a few seconds.
    Rectangle {
        id: toast
        anchors.left: parent.left
        anchors.right: addButton.left
        anchors.bottom: parent.bottom
        anchors.margins: 20
        height: toastText.implicitHeight + 24
        radius: 8
        color: page.theme.pop
        border.color: page.theme.line
        visible: toastText.text.length > 0 && toastTimer.running
        Label {
            id: toastText
            anchors.fill: parent
            anchors.margins: 12
            text: page.app.message
            wrapMode: Text.Wrap
            font.pixelSize: page.theme.small
            color: page.theme.fg
        }
        Timer { id: toastTimer; interval: 6000 }
        Connections {
            target: page.app
            function onMessageChanged() { if (page.app.message.length > 0) toastTimer.restart() }
        }
    }
}

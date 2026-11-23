// What to do with the selected words, after a long press (as in Kindle): highlight in one of four colors (the
// highlight's own color again takes it away), add a note, bookmark the word, the Dictionary card, Translate, Copy. It sits at the top
// or the bottom of the page, away from the finger.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Popup {
    id: bar
    property MobileApp app
    property Theme theme
    property BookPage page
    property bool atTop: false
    property string current: ""    // the selection's highlight color, if it is highlighted
    property bool marked: false    // the word has a word bookmark
    property bool acted: false     // closed by a choice (else the selection goes when it closes)
    signal noteRequested()
    signal dictionaryRequested()
    signal translateRequested()

    function show(pressY) {
        acted = false
        current = app.highlightColorAt(page)
        marked = app.wordBookmarkAt(page)
        atTop = pressY > (parent ? parent.height : 800) / 2
        open()
    }

    x: 8
    y: atTop ? 64 : (parent ? parent.height - height - 40 : 600)
    width: (parent ? parent.width : 400) - 16
    padding: 10
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: bar.theme.pop; radius: 12; border.color: bar.theme.line }
    onClosed: if (!acted && page) page.clearSelection()

    ColumnLayout {
        width: parent.width
        spacing: 6
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 14
            Repeater {
                model: bar.app ? bar.app.highlightColors : []
                delegate: AbstractButton {
                    required property var modelData
                    implicitWidth: 40
                    implicitHeight: 40
                    Accessible.name: bar.current === modelData.key ? qsTr("Remove Highlight") : qsTr("Highlight")
                    objectName: "hl-" + modelData.key
                    contentItem: Item {}
                    background: Rectangle {
                        anchors.centerIn: parent
                        width: 28
                        height: 28
                        radius: 14
                        color: modelData.swatch
                        border.width: bar.current === modelData.key ? 3 : 0
                        border.color: bar.theme.fg
                    }
                    onClicked: {
                        bar.acted = true
                        bar.app.highlight(bar.page, modelData.key)
                        bar.close()
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: bar.theme.line }
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: [
                    {"icon": "note", "label": qsTr("Note"), "act": "note"},
                    {"icon": "bookmark", "label": bar.marked ? qsTr("Unmark") : qsTr("Bookmark"), "act": "bookmark"},
                    {"icon": "dictionary", "label": qsTr("Dictionary"), "act": "dictionary"},
                    {"icon": "translate", "label": qsTr("Translate"), "act": "translate"},
                    {"icon": "copy", "label": qsTr("Copy"), "act": "copy"}
                ]
                delegate: AbstractButton {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 56
                    objectName: "sel-" + modelData.act
                    contentItem: Column {
                        spacing: 2
                        Icon { anchors.horizontalCenter: parent.horizontalCenter; width: 22; height: 22; name: modelData.icon; color: bar.theme.fg }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.label
                            font.pixelSize: 12
                            color: bar.theme.fg
                        }
                    }
                    background: Rectangle { color: parent.pressed ? Qt.rgba(0.5, 0.5, 0.5, 0.15) : "transparent"; radius: 8 }
                    onClicked: {
                        bar.acted = true
                        if (modelData.act === "note") bar.noteRequested()
                        else if (modelData.act === "bookmark") bar.app.toggleWordBookmark(bar.page)
                        else if (modelData.act === "dictionary") bar.dictionaryRequested()
                        else if (modelData.act === "translate") bar.translateRequested()
                        else { bar.app.copyText(bar.page.selectedText); bar.page.clearSelection() }
                        bar.close()
                    }
                }
            }
        }
    }
}

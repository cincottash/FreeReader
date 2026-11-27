// The Dictionary card, sliding up from the bottom in the page's colors: the word's whole entry, laid out like a
// printed dictionary. Full Definition opens Wiktionary; Translate switches to the translation sheet.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Drawer {
    id: sheet
    property MobileApp app
    property Theme theme
    readonly property var c: app ? app.card : ({})
    signal translateRequested()
    signal settingsRequested()

    edge: Qt.BottomEdge
    bottomPadding: app ? app.safeBottom : 0
    width: parent ? parent.width : 400
    height: Math.min(content.implicitHeight + 48, (parent ? parent.height : 800) * 0.7) + bottomPadding
    interactive: opened
    onClosed: if (app) app.closeCard()
    background: Rectangle {
        color: sheet.theme.bg
        radius: 14
        border.color: sheet.theme.line
        Rectangle { width: parent.width; height: 14; anchors.bottom: parent.bottom; color: sheet.theme.bg }
        // The handle: pull down here to close the card.
        Rectangle {
            objectName: "sheetHandle"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 8
            width: 40
            height: 4
            radius: 2
            color: sheet.theme.muted
            opacity: 0.5
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 20
        contentHeight: content.implicitHeight
        clip: true
        ColumnLayout {
            id: content
            width: parent.width
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: sheet.c.title || ""
                    font.pixelSize: 11
                    font.letterSpacing: 1
                    color: sheet.theme.muted
                    wrapMode: Text.Wrap
                }
                // Hear Pronunciation (the phone's voice for the book's language).
                IconButton {
                    objectName: "cardHear"
                    theme: sheet.theme
                    iconName: "speaker"
                    text: qsTr("Hear Pronunciation")
                    visible: (sheet.c.word || "").length > 0
                    onClicked: sheet.app.hear(sheet.c.word)
                }
                // Flip: the card's other side (the book's language, or the reader's), as on the desktop.
                IconButton {
                    objectName: "cardFlip"
                    theme: sheet.theme
                    iconName: "flip"
                    text: qsTr("Flip")
                    visible: (sheet.c.sides || []).length > 1
                    onClicked: sheet.app.flipCard()
                }
            }
            Text {
                objectName: "cardBody"
                Layout.fillWidth: true
                text: sheet.c.html || ""
                textFormat: Text.RichText
                wrapMode: Text.Wrap
                color: sheet.theme.fg
            }
            RowLayout {
                spacing: 16
                Label {
                    visible: (sheet.c.url || "").length > 0
                    text: qsTr("Full Definition")
                    color: sheet.theme.accent
                    font.pixelSize: sheet.theme.body
                    MouseArea { anchors.fill: parent; anchors.margins: -10; onClicked: sheet.app.openLink(sheet.c.url) }
                }
                Label {
                    text: qsTr("Translate")
                    color: sheet.theme.accent
                    font.pixelSize: sheet.theme.body
                    MouseArea { anchors.fill: parent; anchors.margins: -10; onClicked: { sheet.close(); sheet.translateRequested() } }
                }
                Label {
                    visible: sheet.c.offer === true
                    text: qsTr("Get Definitions…")
                    color: sheet.theme.accent
                    font.pixelSize: sheet.theme.body
                    MouseArea { anchors.fill: parent; anchors.margins: -10; onClicked: { sheet.close(); sheet.settingsRequested() } }
                }
            }
            Label {
                Layout.fillWidth: true
                text: sheet.c.source || ""
                font.pixelSize: 11
                color: sheet.theme.muted
                wrapMode: Text.Wrap
            }
        }
    }
}

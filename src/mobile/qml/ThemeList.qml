// Every theme of the desktop app, in its three groups, each with a swatch of its page and accent colors.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

ListView {
    id: list
    property MobileApp app
    property Theme theme
    clip: true
    model: app ? app.themes : []
    delegate: Column {
        id: row
        required property var modelData
        required property int index
        width: ListView.view.width
        // The group's name above its first theme.
        Label {
            visible: row.index === 0 || list.model[row.index - 1].group !== row.modelData.group
            width: parent.width
            topPadding: 12
            bottomPadding: 4
            leftPadding: 4
            text: row.modelData.group
            font.pixelSize: list.theme.small
            font.weight: Font.DemiBold
            color: list.theme.muted
        }
        ItemDelegate {
            width: parent.width
            height: 44
            objectName: "theme-" + row.modelData.key
            contentItem: RowLayout {
                spacing: 12
                Rectangle {
                    width: 22
                    height: 22
                    radius: 11
                    color: row.modelData.bg
                    border.color: list.theme.line
                    Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: row.modelData.accent }
                }
                Label {
                    Layout.fillWidth: true
                    text: row.modelData.name
                    color: list.app.theme === row.modelData.key ? list.theme.accent : list.theme.fg
                    font.weight: list.app.theme === row.modelData.key ? Font.DemiBold : Font.Normal
                }
            }
            background: Rectangle { color: list.app.theme === row.modelData.key ? Qt.rgba(0.5, 0.5, 0.5, 0.15) : "transparent"; radius: 6 }
            onClicked: list.app.theme = row.modelData.key
        }
    }
}

// A finger-sized button with a line icon; `checked` fills it (a bookmarked page).
import QtQuick
import QtQuick.Controls

ToolButton {
    id: b
    property Theme theme
    property string iconName: ""
    property color iconColor: theme ? theme.fg : "black"
    property bool filled: false
    implicitWidth: theme ? theme.touch : 48
    implicitHeight: theme ? theme.touch : 48
    focusPolicy: Qt.NoFocus
    Accessible.name: b.text
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            width: 24
            height: 24
            name: b.iconName
            color: b.iconColor
            filled: b.filled
        }
    }
    background: Rectangle { color: b.pressed ? Qt.rgba(0.5, 0.5, 0.5, 0.18) : "transparent"; radius: width / 2 }
}

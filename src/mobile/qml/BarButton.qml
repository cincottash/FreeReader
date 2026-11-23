// A text button for the top bars, in the theme's colors, at least a finger's size.
import QtQuick
import QtQuick.Controls

ToolButton {
    id: b
    property Theme theme
    property color textColor: theme ? theme.accent : "#B5651D"
    implicitHeight: theme ? theme.touch : 48
    implicitWidth: Math.max(theme ? theme.touch : 48, label.implicitWidth + 24)
    font.pixelSize: theme ? theme.body : 15
    contentItem: Label {
        id: label
        text: b.text
        font: b.font
        color: b.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle { color: b.pressed ? Qt.rgba(0, 0, 0, 0.08) : "transparent"; radius: 6 }
}

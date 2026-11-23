// Colors and sizes shared by the screens, from the reading theme (Paper, Sepia or Night).
import QtQuick

QtObject {
    property var colors: ({})
    readonly property color bg: colors.bg !== undefined ? colors.bg : "#FAF7F0"
    readonly property color fg: colors.fg !== undefined ? colors.fg : "#2B2721"
    readonly property color muted: colors.muted !== undefined ? colors.muted : "#7A7266"
    readonly property color accent: colors.accent !== undefined ? colors.accent : "#B5651D"
    readonly property color line: colors.line !== undefined ? colors.line : "#DDD5C6"
    readonly property color surface: colors.surface !== undefined ? colors.surface : "#F1ECE1"
    readonly property color pop: colors.pop !== undefined ? colors.pop : "#FFFFFF"
    readonly property bool dark: colors.dark === true
    readonly property color accentText: "#FFFFFF"

    readonly property string serif: "Literata"
    readonly property int small: 13
    readonly property int body: 15
    readonly property int title: 19
    readonly property int touch: 48          // the smallest comfortable touch target
}

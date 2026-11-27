// Statistics (as on the desktop): reading time, days in a row, books finished; the minutes read, new words and
// reviews of the last 30 days; each book's time, pages and words. A tap on a book opens it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Page {
    id: stats
    property MobileApp app
    property Theme theme
    signal closeRequested()
    property var s: ({})
    function refresh() { s = stats.app.statistics() }
    Component.onCompleted: refresh()

    background: Rectangle { color: stats.theme.bg }
    header: Rectangle {
        height: 56
        color: stats.theme.surface
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            BarButton { text: qsTr("‹ Back"); theme: stats.theme; onClicked: stats.closeRequested() }
            Label { text: qsTr("Statistics"); font.pixelSize: stats.theme.title; font.weight: Font.DemiBold; color: stats.theme.fg; Layout.fillWidth: true }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: stats.theme.line }
    }

    component Section: Label {
        Layout.topMargin: 16
        font.pixelSize: 12
        font.weight: Font.DemiBold
        font.letterSpacing: 1
        color: stats.theme.muted
    }
    component Tile: Rectangle {
        property alias value: v.text
        property alias caption: c.text
        Layout.fillWidth: true
        implicitHeight: col.implicitHeight + 20
        radius: 8
        color: stats.theme.surface
        border.color: stats.theme.line
        ColumnLayout {
            id: col
            x: 12
            y: 10
            width: parent.width - 24
            spacing: 2
            Label { id: v; font.pixelSize: 22; font.weight: Font.DemiBold; color: stats.theme.fg }
            Label { id: c; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12; color: stats.theme.muted }
        }
    }
    // Bars for the last 30 days, the highest one full height; its total underneath.
    component Bars: ColumnLayout {
        id: bars
        property string role
        property string unit
        spacing: 2
        readonly property var days: stats.s.days || []
        readonly property real most: { let m = 0; for (let i = 0; i < days.length; ++i) m = Math.max(m, days[i][role]); return m }
        readonly property real total: { let t = 0; for (let i = 0; i < days.length; ++i) t += days[i][role]; return t }
        Layout.fillWidth: true
        Row {
            id: barRow
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            spacing: 2
            Repeater {
                model: bars.days
                delegate: Item {
                    required property var modelData
                    width: Math.max(2, (barRow.width - 2 * 29) / 30)
                    height: 70
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        radius: 1
                        readonly property real v: modelData[bars.role]
                        height: bars.most > 0 ? Math.max(v > 0 ? 2 : 0, 66 * v / bars.most) : 0
                        color: stats.theme.accent
                    }
                }
            }
        }
        Label {
            text: qsTr("Total: %1 %2").arg(Math.round(bars.total)).arg(bars.unit)
            font.pixelSize: 12
            color: stats.theme.muted
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: body.implicitHeight + 40
        clip: true
        ColumnLayout {
            id: body
            x: 16
            y: 8
            width: parent.width - 32
            spacing: 8

            Label {
                objectName: "statsEmpty"
                Layout.fillWidth: true
                visible: stats.s.empty === true
                wrapMode: Text.Wrap
                color: stats.theme.muted
                text: qsTr("Nothing yet. Reading time is counted as you turn pages; words and reviews come from translating, highlighting and practicing.")
            }

            Section { text: qsTr("READING") }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                Tile { objectName: "statToday"; value: stats.s.today || ""; caption: qsTr("Read Today") }
                Tile { objectName: "statWeek"; value: stats.s.week || ""; caption: qsTr("Read in the Last 7 Days") }
                Tile { objectName: "statStreak"; value: stats.s.streak || ""; caption: qsTr("Days in a Row") }
                Tile { objectName: "statFinished"; value: stats.s.finished || ""; caption: qsTr("Books Finished") }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: stats.s.summary || ""; font.pixelSize: 12; color: stats.theme.muted }

            Section { text: qsTr("MINUTES READ, LAST 30 DAYS") }
            Bars { role: "minutes"; unit: qsTr("min") }

            Section { text: qsTr("LEARNING") }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                Tile { objectName: "statWords"; value: stats.s.words || ""; caption: qsTr("Words in the Word Log") }
                Tile { objectName: "statLearned"; value: stats.s.learned || ""; caption: qsTr("Cards Learned") }
                Tile { objectName: "statReviewsToday"; value: stats.s.reviewsToday || ""; caption: qsTr("Reviews Today") }
                Tile { objectName: "statRetention"; value: stats.s.retention || ""; caption: qsTr("Remembered, Last 30 Days") }
            }
            Section { text: qsTr("NEW WORDS, LAST 30 DAYS") }
            Bars { role: "words"; unit: qsTr("words") }
            Section { text: qsTr("REVIEWS, LAST 30 DAYS") }
            Bars { role: "reviews"; unit: qsTr("reviews") }

            Section { text: qsTr("BOOKS"); visible: (stats.s.books || []).length > 0 }
            Repeater {
                model: stats.s.books || []
                delegate: ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    leftPadding: 0
                    contentItem: ColumnLayout {
                        spacing: 1
                        Label { Layout.fillWidth: true; text: modelData.title; elide: Text.ElideRight; font.weight: Font.DemiBold; color: stats.theme.fg }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("%1 · %2 pages · %3 words · %4").arg(modelData.time).arg(modelData.pages).arg(modelData.words).arg(modelData.progress)
                            font.pixelSize: 12
                            color: stats.theme.muted
                        }
                    }
                    onClicked: stats.app.openBook(modelData.id)
                }
            }
        }
    }
}

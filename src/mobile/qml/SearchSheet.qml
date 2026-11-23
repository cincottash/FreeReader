// Search This Book: type, then tap a match to go there (the matches stay for the next one).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Drawer {
    id: sheet
    property MobileApp app
    property Theme theme

    edge: Qt.TopEdge
    width: parent ? parent.width : 400
    height: (parent ? parent.height : 800) * 0.75
    background: Rectangle { color: sheet.theme.surface }
    onOpened: field.forceActiveFocus()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: field
                objectName: "searchField"
                Layout.fillWidth: true
                placeholderText: qsTr("Search this book")
                inputMethodHints: Qt.ImhNoPredictiveText
                color: sheet.theme.fg
                onAccepted: sheet.app.search(text)
            }
            IconButton { theme: sheet.theme; iconName: "search"; text: qsTr("Search"); onClicked: sheet.app.search(field.text) }
        }
        Label {
            text: sheet.app ? sheet.app.searchSummary : ""
            color: sheet.theme.muted
            font.pixelSize: sheet.theme.small
        }
        ListView {
            objectName: "searchResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: sheet.app ? sheet.app.searchResults : []
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: ListView.view.width
                contentItem: ColumnLayout {
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.StyledText
                        wrapMode: Text.Wrap
                        font.family: sheet.theme.serif
                        font.pixelSize: sheet.theme.body
                        color: sheet.theme.fg
                        text: "…" + modelData.before.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "<b>" +
                              modelData.match.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</b>" +
                              modelData.after.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "…"
                    }
                    Label { text: modelData.page ? qsTr("Page %1").arg(modelData.page) : ""; font.pixelSize: 12; color: sheet.theme.muted }
                }
                onClicked: { sheet.app.goToSearchResult(index); sheet.close() }
            }
        }
    }
}

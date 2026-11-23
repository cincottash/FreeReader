// The book's Contents and its Notebook (highlights, notes and bookmarks), sliding in from the left. Tapping an
// entry goes there; a highlight or bookmark can be deleted from its row.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Drawer {
    id: nav
    property MobileApp app
    property Theme theme
    property int tab: 0   // 0 Contents, 1 Notebook

    edge: Qt.LeftEdge
    topPadding: app ? app.safeTop : 0
    bottomPadding: app ? app.safeBottom : 0
    width: Math.min(360, (parent ? parent.width : 400) * 0.88)
    height: parent ? parent.height : 800
    background: Rectangle { color: nav.theme.surface }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TabBar {
            id: tabs
            Layout.fillWidth: true
            currentIndex: nav.tab
            onCurrentIndexChanged: nav.tab = currentIndex
            background: Rectangle { color: nav.theme.surface }
            Repeater {
                model: [qsTr("Contents"), qsTr("Notebook")]
                delegate: TabButton {
                    required property string modelData
                    required property int index
                    objectName: index === 0 ? "tabContents" : "tabNotebook"
                    text: modelData
                    height: 48
                    contentItem: Label {
                        text: parent.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: nav.theme.body
                        font.weight: nav.tab === index ? Font.DemiBold : Font.Normal
                        color: nav.tab === index ? nav.theme.accent : nav.theme.muted
                    }
                    background: Rectangle {
                        color: nav.theme.surface
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: nav.theme.accent; visible: nav.tab === index }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: nav.theme.line }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: nav.tab

            ListView {
                id: contents
                objectName: "contentsList"
                clip: true
                model: nav.app ? nav.app.contents : []
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    leftPadding: 16 + 16 * modelData.depth
                    contentItem: Label {
                        text: modelData.title
                        wrapMode: Text.Wrap
                        font.pixelSize: nav.theme.body
                        font.weight: modelData.current ? Font.DemiBold : Font.Normal
                        color: modelData.current ? nav.theme.accent : nav.theme.fg
                    }
                    background: Rectangle { color: parent.pressed ? Qt.rgba(0.5, 0.5, 0.5, 0.15) : "transparent" }
                    onClicked: { nav.app.goToContents(index); nav.close() }
                }
                Label {
                    anchors.centerIn: parent
                    visible: contents.count === 0
                    text: qsTr("This book has no contents.")
                    color: nav.theme.muted
                }
            }

            ListView {
                id: notes
                objectName: "notebookList"
                clip: true
                spacing: 2
                model: nav.app ? nav.app.notebook : []
                delegate: Item {
                    id: entry
                    required property var modelData
                    width: ListView.view.width
                    height: rowLayout.implicitHeight + 24
                    Rectangle { anchors.fill: parent; color: tap.pressed ? Qt.rgba(0.5, 0.5, 0.5, 0.15) : "transparent" }
                    MouseArea { id: tap; anchors.fill: parent; onClicked: { nav.app.goToAnnotation(entry.modelData.id); nav.close() } }
                    RowLayout {
                        id: rowLayout
                        x: 12
                        y: 12
                        width: parent.width - 24
                        spacing: 12
                        Rectangle {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 4
                            width: 14
                            height: 14
                            radius: entry.modelData.kind === "bookmark" ? 2 : 7
                            color: entry.modelData.kind === "bookmark" ? "transparent" : entry.modelData.swatch
                            border.width: entry.modelData.kind === "bookmark" ? 2 : 0
                            border.color: entry.modelData.swatch
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: entry.modelData.kind !== "bookmark" ? "“" + entry.modelData.text + "”"
                                      : entry.modelData.word ? qsTr("Word Bookmark · %1").arg(entry.modelData.text)
                                                             : qsTr("Bookmark · %1").arg(entry.modelData.text)
                                wrapMode: Text.Wrap
                                maximumLineCount: 4
                                elide: Text.ElideRight
                                font.family: nav.theme.serif
                                font.pixelSize: nav.theme.body
                                color: nav.theme.fg
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: entry.modelData.note.length > 0
                                text: "✎ " + entry.modelData.note
                                wrapMode: Text.Wrap
                                font.pixelSize: nav.theme.small
                                color: nav.theme.fg
                            }
                            Label {
                                Layout.fillWidth: true
                                text: (entry.modelData.page ? qsTr("Page %1").arg(entry.modelData.page) : "") + (entry.modelData.chapter ? " · " + entry.modelData.chapter : "")
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                font.pixelSize: 12
                                color: nav.theme.muted
                            }
                        }
                        IconButton {
                            Layout.alignment: Qt.AlignTop
                            theme: nav.theme
                            iconName: "trash"
                            iconColor: nav.theme.muted
                            text: qsTr("Delete")
                            onClicked: nav.app.removeAnnotation(entry.modelData.id)
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    visible: notes.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("No highlights yet. Press and hold a word, then pick a color.")
                    color: nav.theme.muted
                }
            }
        }
    }
}

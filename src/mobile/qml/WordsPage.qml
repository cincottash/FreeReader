// Words: Practice (flashcards with spaced repetition, the same cards and schedule as the desktop) and the Word List
// (every word looked up or highlighted, with its meaning and the sentence it was in; star or delete one).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Page {
    id: words
    property MobileApp app
    property Theme theme
    property int tab: 0   // 0 Practice, 1 Word List
    signal closeRequested()
    readonly property var p: app ? app.practice : ({})

    function startPractice() { tab = 0; app.startPractice() }

    background: Rectangle { color: words.theme.bg }
    Component.onCompleted: app.refreshPractice()
    StackView.onRemoved: app.endPractice()

    header: Rectangle {
        height: 104
        color: words.theme.surface
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                IconButton { theme: words.theme; iconName: "back"; text: qsTr("Back"); onClicked: words.closeRequested() }
                Label { text: qsTr("Words"); font.pixelSize: words.theme.title; font.weight: Font.DemiBold; color: words.theme.fg; Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                spacing: 0
                Repeater {
                    model: [qsTr("Practice"), qsTr("Word List (%1)").arg(words.app ? words.app.wordCount : 0)]
                    delegate: AbstractButton {
                        required property string modelData
                        required property int index
                        objectName: index === 0 ? "tabPractice" : "tabWordList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentItem: Label {
                            text: modelData
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: words.theme.body
                            font.weight: words.tab === index ? Font.DemiBold : Font.Normal
                            color: words.tab === index ? words.theme.accent : words.theme.muted
                        }
                        background: Rectangle {
                            color: words.theme.surface
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: words.theme.accent; visible: words.tab === index }
                        }
                        onClicked: words.tab = index
                    }
                }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: words.theme.line }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: words.tab

        // ---- Practice
        Item {
            // The start: what is due, and Start.
            ColumnLayout {
                visible: !words.p.active
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 14
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: (words.p.summary || "").length > 0 ? words.p.summary
                          : qsTr("%1 due now · %2 new").arg(words.p.dueNow || 0).arg(words.p.newAvailable || 0)
                    font.pixelSize: words.theme.body
                    color: words.theme.fg
                }
                Button {
                    objectName: "startPractice"
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Start Practice")
                    highlighted: true
                    enabled: (words.p.dueNow || 0) + (words.p.newAvailable || 0) > 0
                    onClicked: words.app.startPractice()
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: (words.p.words || 0) === 0
                    text: qsTr("Words you look up or highlight while reading become flashcards here.")
                    font.pixelSize: words.theme.small
                    color: words.theme.muted
                }
            }

            // A card.
            ColumnLayout {
                visible: words.p.active === true
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: words.p.direction || ""; color: words.theme.muted; font.pixelSize: words.theme.small; Layout.fillWidth: true }
                    Label { text: words.p.progress || ""; color: words.theme.muted; font.pixelSize: words.theme.small }
                }
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: card.implicitHeight
                    clip: true
                    ColumnLayout {
                        id: card
                        width: parent.width
                        spacing: 10
                        Label {
                            objectName: "cardFront"
                            Layout.fillWidth: true
                            Layout.topMargin: 24
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: words.p.front || ""
                            font.family: words.theme.serif
                            font.pixelSize: words.p.frontIsLong ? 19 : 30
                            color: words.theme.fg
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: text.length > 0
                            text: words.p.frontDetail || ""
                            font.pixelSize: words.theme.small
                            color: words.theme.muted
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: text.length > 0
                            textFormat: Text.StyledText
                            text: words.p.frontSentence || ""
                            font.family: words.theme.serif
                            font.pixelSize: words.theme.body
                            color: words.theme.fg
                        }
                        Rectangle { Layout.fillWidth: true; Layout.topMargin: 8; height: 1; color: words.theme.line; visible: words.p.revealed === true }
                        Label {
                            objectName: "cardBack"
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: words.p.revealed === true
                            text: words.p.back || ""
                            font.family: words.theme.serif
                            font.pixelSize: 22
                            color: words.theme.accent
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: words.p.revealed === true && text.length > 0
                            text: words.p.backDetail || ""
                            font.pixelSize: words.theme.small
                            color: words.theme.fg
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: words.p.revealed === true && text.length > 0
                            textFormat: Text.StyledText
                            text: words.p.backSentence || ""
                            font.family: words.theme.serif
                            font.pixelSize: words.theme.body
                            color: words.theme.fg
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            visible: words.p.revealed === true && text.length > 0
                            text: words.p.source || ""
                            font.pixelSize: 12
                            color: words.theme.muted
                        }
                    }
                }
                Button {
                    objectName: "showAnswer"
                    Layout.fillWidth: true
                    visible: words.p.revealed !== true
                    text: qsTr("Show Answer")
                    highlighted: true
                    implicitHeight: words.theme.touch + 4
                    onClicked: words.app.revealCard()
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: words.p.revealed === true
                    spacing: 6
                    Repeater {
                        model: [qsTr("Again"), qsTr("Hard"), qsTr("Good"), qsTr("Easy")]
                        delegate: Button {
                            required property string modelData
                            required property int index
                            objectName: "rate" + (index + 1)
                            Layout.fillWidth: true
                            implicitHeight: 60
                            highlighted: index === 2
                            contentItem: Column {
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData; font.pixelSize: words.theme.body; color: index === 2 ? "white" : words.theme.fg }
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: (words.p.intervals || [])[index] || ""
                                    font.pixelSize: 11
                                    color: index === 2 ? "white" : words.theme.muted
                                }
                            }
                            onClicked: words.app.rateCard(index + 1)
                        }
                    }
                }
            }
        }

        // ---- Word List
        ListView {
            id: list
            objectName: "wordList"
            clip: true
            model: words.app ? words.app.words : []
            delegate: Item {
                id: entry
                required property var modelData
                width: ListView.view.width
                height: col.implicitHeight + 24
                RowLayout {
                    x: 16
                    y: 12
                    width: parent.width - 24
                    spacing: 8
                    ColumnLayout {
                        id: col
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: entry.modelData.headword + (entry.modelData.translation ? " — " + entry.modelData.translation : "")
                            wrapMode: Text.Wrap
                            font.family: words.theme.serif
                            font.pixelSize: words.theme.body
                            color: words.theme.fg
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: entry.modelData.needsMeaning
                            text: qsTr("No meaning yet: look it up while reading.")
                            font.pixelSize: 12
                            color: words.theme.muted
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: entry.modelData.sentence.length > 0
                            text: entry.modelData.sentence
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            font.family: words.theme.serif
                            font.italic: true
                            font.pixelSize: words.theme.small
                            color: words.theme.muted
                        }
                        Label {
                            Layout.fillWidth: true
                            text: entry.modelData.book + (entry.modelData.page ? " · " + qsTr("p. %1").arg(entry.modelData.page) : "")
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            font.pixelSize: 12
                            color: words.theme.muted
                        }
                    }
                    IconButton {
                        Layout.alignment: Qt.AlignTop
                        theme: words.theme
                        iconName: "bookmark"
                        text: entry.modelData.starred ? qsTr("Unstar") : qsTr("Star")
                        filled: entry.modelData.starred
                        iconColor: entry.modelData.starred ? words.theme.accent : words.theme.muted
                        onClicked: words.app.setWordStarred(entry.modelData.id, !entry.modelData.starred)
                    }
                    IconButton {
                        Layout.alignment: Qt.AlignTop
                        theme: words.theme
                        iconName: "trash"
                        iconColor: words.theme.muted
                        text: qsTr("Delete")
                        onClicked: words.app.removeWord(entry.modelData.id)
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; x: 16; width: parent.width - 32; height: 1; color: words.theme.line }
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 48
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("No words yet. Press and hold a word while reading to look it up or highlight it.")
                color: words.theme.muted
            }
        }
    }
}

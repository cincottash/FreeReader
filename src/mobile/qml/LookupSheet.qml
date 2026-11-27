// The answer to a long press, sliding up from the bottom: the word, its dictionary form, the English meaning in
// this sentence, other meanings and the Spanish definition. Translate Sentence looks up the whole sentence.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Drawer {
    id: sheet
    property MobileApp app
    property Theme theme
    property BookPage page
    readonly property var r: app ? app.lookup : ({})

    edge: Qt.BottomEdge
    bottomPadding: app ? app.safeBottom : 0
    width: parent ? parent.width : 400
    height: Math.min(content.implicitHeight + 40, (parent ? parent.height : 800) * 0.6) + bottomPadding
    interactive: opened
    background: Rectangle {
        color: sheet.theme.pop
        radius: 14
        border.color: sheet.theme.line
        Rectangle { width: parent.width; height: 14; anchors.bottom: parent.bottom; color: sheet.theme.pop }
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
                    text: sheet.r.text || ""
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    font.family: sheet.theme.serif
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    color: sheet.theme.fg
                }
                Label {
                    visible: (sheet.r.lemma || "").length > 0
                    text: "→ " + (sheet.r.lemma || "")
                    font.family: sheet.theme.serif
                    font.pixelSize: 16
                    color: sheet.theme.muted
                }
            }
            Label {
                visible: (sheet.r.partOfSpeech || "").length > 0
                text: sheet.r.partOfSpeech || ""
                font.italic: true
                font.pixelSize: sheet.theme.small
                color: sheet.theme.muted
            }
            BusyIndicator { visible: sheet.r.loading === true; running: visible; Layout.alignment: Qt.AlignHCenter }
            Label {
                Layout.fillWidth: true
                visible: (sheet.r.translation || "").length > 0
                text: sheet.r.translation || ""
                wrapMode: Text.Wrap
                font.pixelSize: 18
                color: sheet.theme.fg
            }
            Label {
                Layout.fillWidth: true
                visible: (sheet.r.alternatives || "").length > 0
                text: qsTr("Also: %1").arg(sheet.r.alternatives || "")
                wrapMode: Text.Wrap
                font.pixelSize: sheet.theme.small
                color: sheet.theme.muted
            }
            Label {
                Layout.fillWidth: true
                visible: (sheet.r.definition || "").length > 0
                text: sheet.r.definition || ""
                wrapMode: Text.Wrap
                font.family: sheet.theme.serif
                font.italic: true
                font.pixelSize: sheet.theme.body
                color: sheet.theme.fg
            }
            Label {
                Layout.fillWidth: true
                visible: (sheet.r.inSentence || "").length > 0 && sheet.r.isWord === true
                text: sheet.r.inSentence || ""
                wrapMode: Text.Wrap
                font.pixelSize: sheet.theme.small
                color: sheet.theme.muted
            }
            Label {
                Layout.fillWidth: true
                visible: (sheet.r.note || "").length > 0
                text: sheet.r.note || ""
                wrapMode: Text.Wrap
                font.pixelSize: sheet.theme.small
                color: sheet.theme.muted
            }
            Label {
                Layout.fillWidth: true
                visible: (sheet.r.nothing || "").length > 0
                text: sheet.r.nothing || ""
                wrapMode: Text.Wrap
                font.pixelSize: sheet.theme.small
                color: sheet.theme.fg
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Label {
                    Layout.fillWidth: true
                    text: sheet.r.source || ""
                    font.pixelSize: 11
                    color: sheet.theme.muted
                }
                // An AI's answer can be reported (Google Play's AI-generated content policy): a new GitHub issue,
                // filled in, opens in the browser; nothing is sent until it is submitted there.
                Button {
                    objectName: "reportAnswer"
                    visible: sheet.r.ai === true && sheet.r.loading !== true
                    flat: true
                    text: qsTr("Report")
                    onClicked: sheet.app.reportAnswer()
                }
                Button {
                    visible: sheet.r.isWord === true && sheet.r.loading !== true
                    text: qsTr("Translate Sentence")
                    onClicked: {
                        sheet.page.selectSentence()
                        sheet.app.lookUp(sheet.page)
                    }
                }
            }
        }
    }
}

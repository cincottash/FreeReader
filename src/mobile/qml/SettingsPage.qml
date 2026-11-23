// Settings: reading (text size, margins, theme), translation (the Gemini key, the offline dictionary) and About.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Page {
    id: settings
    property MobileApp app
    property Theme theme
    signal closeRequested()

    background: Rectangle { color: settings.theme.bg }
    header: Rectangle {
        height: 56
        color: settings.theme.surface
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            BarButton {
                text: qsTr("‹ Back")
                theme: settings.theme
                onClicked: settings.closeRequested()
            }
            Label { text: qsTr("Settings"); font.pixelSize: settings.theme.title; font.weight: Font.DemiBold; color: settings.theme.fg; Layout.fillWidth: true }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: settings.theme.line }
    }

    component Section: Label {
        Layout.topMargin: 18
        font.pixelSize: 12
        font.weight: Font.DemiBold
        font.letterSpacing: 1
        color: settings.theme.muted
    }
    component Note: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: settings.theme.small
        color: settings.theme.muted
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 40
        clip: true
        ColumnLayout {
            id: col
            x: 20
            width: parent.width - 40
            spacing: 10

            Section { text: qsTr("READING") }
            Label { text: qsTr("Text Size: %1 %").arg(settings.app.textSize); color: settings.theme.fg }
            Slider { Layout.fillWidth: true; from: 60; to: 250; stepSize: 10; value: settings.app.textSize; onMoved: settings.app.textSize = value }
            Label { text: qsTr("Margins: %1 px").arg(settings.app.margins); color: settings.theme.fg }
            Slider { Layout.fillWidth: true; from: 4; to: 80; stepSize: 4; value: settings.app.margins; onMoved: settings.app.margins = value }
            RowLayout {
                spacing: 8
                Repeater {
                    model: [["paper", qsTr("Paper")], ["sepia", qsTr("Sepia")], ["night", qsTr("Night")]]
                    delegate: Button {
                        required property var modelData
                        text: modelData[1]
                        checkable: true
                        checked: settings.app.theme === modelData[0]
                        onClicked: settings.app.theme = modelData[0]
                    }
                }
            }

            Section { text: qsTr("GEMINI (GOOGLE, ONLINE)") }
            Note {
                text: settings.app.keyStoreAvailable
                      ? qsTr("Translates words and sentences in context. The free tier needs no card. The key is kept in the phone's key store.")
                      : qsTr("Translates words and sentences in context. The free tier needs no card. This phone's key store isn't available yet, so a key works until the app closes.")
            }
            Label { text: settings.app.hasGeminiKey ? qsTr("A key is saved.") : qsTr("No key yet."); color: settings.theme.fg }
            TextField {
                id: keyField
                Layout.fillWidth: true
                placeholderText: qsTr("Paste a Gemini API key")
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            }
            RowLayout {
                Button {
                    text: qsTr("Save Key")
                    onClicked: { keyMessage.text = settings.app.saveGeminiKey(keyField.text) || qsTr("Saved."); keyField.clear() }
                }
                Button { text: qsTr("Remove Key"); enabled: settings.app.hasGeminiKey; onClicked: settings.app.removeGeminiKey() }
                Button { text: qsTr("Get a Free Key"); flat: true; onClicked: settings.app.openLink("https://aistudio.google.com/apikey") }
            }
            Note { id: keyMessage; visible: text.length > 0 }

            Section { text: qsTr("OFFLINE DICTIONARY") }
            Note { text: qsTr("Answers single words with no internet, and while Gemini's answer is on its way. Made from Wiktionary (CC BY-SA 4.0).") }
            Label { text: settings.app.dictionaryStatus; color: settings.theme.fg; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ProgressBar { Layout.fillWidth: true; visible: settings.app.downloadProgress >= 0; value: Math.max(0, settings.app.downloadProgress) }
            Button {
                visible: !settings.app.dictionaryInstalled
                text: settings.app.downloadProgress >= 0 ? qsTr("Cancel Download") : qsTr("Download")
                onClicked: settings.app.downloadProgress >= 0 ? settings.app.cancelDownload() : settings.app.downloadDictionary()
            }

            Section { text: qsTr("ABOUT") }
            Note { text: qsTr("FreeReader %1 · © cincottash · for DRM-free EPUB books.").arg(settings.app.version) }
        }
    }
}

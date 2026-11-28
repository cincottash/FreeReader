// Settings: reading (text size, margins, theme), translation (the Gemini key, the offline dictionaries) and About.
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

            // Your language: the app's text and every answer in it (as on the desktop, Settings › General).
            Section { text: qsTr("LANGUAGE") }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: settings.app.readerLanguages()
                    delegate: Button {
                        required property var modelData
                        objectName: "language-" + modelData.code
                        Layout.fillWidth: true
                        text: modelData.name
                        highlighted: settings.app.readerLanguage === modelData.code
                        onClicked: settings.app.readerLanguage = modelData.code
                    }
                }
            }
            Note { text: qsTr("Your language: the app's text, and the language of the translations and explanations.") }

            Section { text: qsTr("READING") }
            Label { text: qsTr("Text Size: %1 %").arg(settings.app.textSize); color: settings.theme.fg }
            Slider { Layout.fillWidth: true; from: 60; to: 250; stepSize: 10; value: settings.app.textSize; onMoved: settings.app.textSize = value }
            Label { text: qsTr("Margins: %1 px").arg(settings.app.margins); color: settings.theme.fg }
            Slider { Layout.fillWidth: true; from: 8; to: 80; stepSize: 4; value: settings.app.margins; onMoved: settings.app.margins = value }
            // The theme: one line with the current one, folded by default; a tap opens every theme (the owner's
            // request, 2026-11-27: the whole list was in the way of the settings below it).
            ItemDelegate {
                id: themeToggle
                objectName: "settingsThemeToggle"
                property bool open: false
                Layout.fillWidth: true
                leftPadding: 0
                rightPadding: 4
                onClicked: open = !open
                contentItem: RowLayout {
                    spacing: 8
                    Label { text: qsTr("Theme"); color: settings.theme.fg; Layout.fillWidth: true }
                    Label {
                        objectName: "settingsThemeName"
                        text: {
                            const list = settings.app ? settings.app.themes : []
                            for (let i = 0; i < list.length; ++i)
                                if (list[i].key === settings.app.theme) return list[i].name
                            return ""
                        }
                        color: settings.theme.accent
                    }
                    Label { text: themeToggle.open ? "▴" : "▾"; color: settings.theme.muted }
                }
                background: Rectangle { color: themeToggle.pressed ? Qt.rgba(0.5, 0.5, 0.5, 0.15) : "transparent"; radius: 6 }
            }
            ThemeList {
                // Every theme shown when open, the page scrolling (a list of its own 300 px high hid Green Tea and the
                // groups after it behind a second scroll inside the page, 2026-11-27).
                objectName: "settingsThemes"
                visible: themeToggle.open
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? contentHeight : 0
                interactive: false
                app: settings.app
                theme: settings.theme
            }

            Section { text: qsTr("GEMINI (GOOGLE, ONLINE)") }
            Note {
                text: settings.app.keyStoreAvailable
                      ? qsTr("Translates words and sentences in context. The free tier needs no card. The key is kept in the phone's key store.")
                      : qsTr("Translates words and sentences in context. The free tier needs no card. This phone's key store isn't available yet, so a key works until the app closes.")
            }
            // The same switches as the desktop's Settings › Translation.
            Switch {
                objectName: "optUseGemini"
                text: qsTr("Use Gemini")
                checked: settings.app.options.useGemini === true
                onToggled: settings.app.setOption("useGemini", checked)
            }
            Switch {
                objectName: "optExplainOnly"
                text: qsTr("Use Gemini Only for Explain This Sentence")
                enabled: settings.app.options.useGemini === true
                checked: settings.app.options.geminiExplainOnly === true
                onToggled: settings.app.setOption("geminiExplainOnly", checked)
                Layout.fillWidth: true
                contentItem: Label {
                    leftPadding: parent.indicator.width + parent.spacing
                    text: parent.text
                    wrapMode: Text.Wrap
                    color: settings.theme.fg
                    opacity: parent.enabled ? 1 : 0.5
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Note {
                visible: settings.app.options.geminiExplainOnly === true && settings.app.options.useGemini === true
                text: qsTr("Gemini then answers only Explain This Sentence. Words and sentences come from LM Studio (when set up below) and the offline dictionaries.")
            }
            Switch {
                objectName: "optBookOffline"
                visible: settings.app.reading
                text: qsTr("Keep This Book Offline")
                enabled: settings.app.options.useGemini === true
                checked: settings.app.options.bookOffline === true
                onToggled: settings.app.setOption("bookOffline", checked)
            }
            Note {
                visible: settings.app.reading
                text: qsTr("Open now: %1. Text from a book kept offline is never sent to Gemini.").arg(settings.app.bookTitle)
            }
            Label { text: settings.app.hasGeminiKey ? qsTr("A key is saved.") : qsTr("No key yet."); color: settings.theme.fg }
            TextField {
                id: keyField
                Layout.fillWidth: true
                placeholderText: qsTr("Paste a Gemini API Key")
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
            Label { text: qsTr("Model"); color: settings.theme.fg }
            ComboBox {
                objectName: "optGeminiModel"
                Layout.fillWidth: true
                editable: true
                model: ["gemini-flash-lite-latest", "gemini-3.5-flash-lite", "gemini-3.1-flash-lite", "gemini-flash-latest"]
                Component.onCompleted: editText = settings.app.options.geminiModel
                onAccepted: settings.app.setOption("geminiModel", editText)
                onActivated: settings.app.setOption("geminiModel", currentText)
            }
            Note { text: qsTr("gemini-flash-lite-latest always uses Google's newest Flash-Lite model, which has the most free lookups a day.") }
            RowLayout {
                Label { text: qsTr("Wait"); color: settings.theme.fg; Layout.fillWidth: true }
                SpinBox {
                    objectName: "optGeminiWait"
                    from: 0
                    to: 300
                    stepSize: 5
                    value: Math.round((settings.app.options.geminiWaitSec || 0) * 10)
                    textFromValue: (v) => v === 0 ? qsTr("No Limit") : qsTr("%1 s").arg((v / 10).toLocaleString(Qt.locale(), 'f', 1))
                    valueFromText: (t) => Math.round(Number.fromLocaleString(Qt.locale(), t.replace(/[^0-9.,]/g, "")) * 10)
                    onValueModified: settings.app.setOption("geminiWaitSec", value / 10)
                }
            }
            Note { text: qsTr("A word or passage Gemini hasn't answered by then is answered by the next engine; Gemini's answer, when it comes, is kept for next time.") }
            RowLayout {
                Button {
                    objectName: "testGemini"
                    text: qsTr("Test")
                    enabled: settings.app.hasGeminiKey
                    onClicked: { testResult.text = qsTr("Testing…"); settings.app.testGemini() }
                }
                Note { id: testResult; Layout.fillWidth: true }
            }
            Connections {
                target: settings.app
                function onGeminiTested(result) { testResult.text = result }
                function onLmStudioChecked(result, models) {
                    lmResult.text = result
                    if (models.length > 0) lmModel.model = models
                }
            }

            // LM Studio on a computer in the same Wi-Fi (as on the desktop; after Gemini, before the dictionaries).
            Section { text: qsTr("LM STUDIO (A COMPUTER ON THIS WI-FI)") }
            Note { text: qsTr("Translations from a model running on your computer, free and private. In LM Studio on the computer: Developer › Start Server, with Serve on Local Network on; then type the address it shows.") }
            Switch {
                objectName: "optUseLmStudio"
                text: qsTr("Use LM Studio")
                checked: settings.app.options.useLmStudio === true
                onToggled: settings.app.setOption("useLmStudio", checked)
            }
            TextField {
                id: lmAddress
                objectName: "optLmAddress"
                Layout.fillWidth: true
                enabled: settings.app.options.useLmStudio === true
                placeholderText: qsTr("LM Studio's Address (http://192.168.1.20:1234)")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                text: settings.app.options.lmStudioAddress || ""
                color: settings.theme.fg
                onEditingFinished: settings.app.setOption("lmStudioAddress", text)
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Model"); color: settings.theme.fg }
                ComboBox {
                    id: lmModel
                    objectName: "optLmModel"
                    Layout.fillWidth: true
                    enabled: settings.app.options.useLmStudio === true
                    textRole: "label"
                    valueRole: "id"
                    model: [{ "id": settings.app.options.lmStudioModel || "",
                              "label": (settings.app.options.lmStudioModel || "").length > 0 ? settings.app.options.lmStudioModel : qsTr("The Model Loaded in LM Studio") }]
                    onActivated: (i) => settings.app.setOption("lmStudioModel", model[i].id)
                }
            }
            RowLayout {
                Button {
                    objectName: "checkLmStudio"
                    text: qsTr("Check")
                    enabled: settings.app.options.useLmStudio === true
                    onClicked: { lmResult.text = qsTr("Checking…"); settings.app.checkLmStudio(lmAddress.text) }
                }
                Note { id: lmResult; objectName: "lmResult"; Layout.fillWidth: true }
            }

            Section { text: qsTr("OFFLINE DICTIONARIES") }
            // What happened with the last download (installed, or why it failed).
            Label {
                objectName: "settingsMessage"
                Layout.fillWidth: true
                visible: settings.app.message.length > 0
                text: settings.app.message
                wrapMode: Text.Wrap
                font.pixelSize: settings.theme.small
                font.weight: Font.DemiBold
                color: settings.theme.fg
            }
            Note { text: qsTr("Optional downloads that work with no internet. Each is made from free data (Wiktionary, CC BY-SA 4.0; Open English WordNet, CC BY 4.0).") }
            Switch {
                objectName: "optUseDictionary"
                text: qsTr("Use the Offline Dictionaries")
                checked: settings.app.options.useDictionary === true
                onToggled: settings.app.setOption("useDictionary", checked)
            }
            Repeater {
                model: settings.app.dictionaries
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 4
                    Label { text: modelData.title; font.weight: Font.DemiBold; color: settings.theme.fg }
                    Note { text: modelData.about + " " + modelData.status }
                    ProgressBar { Layout.fillWidth: true; visible: modelData.downloading; value: Math.max(0, modelData.progress) }
                    RowLayout {
                        Button {
                            objectName: "download-" + modelData.key
                            visible: !modelData.installed
                            text: modelData.downloading ? qsTr("Cancel Download") : qsTr("Download (%1 MB)").arg(modelData.sizeMb)
                            onClicked: modelData.downloading ? settings.app.cancelDownload(modelData.key) : settings.app.downloadDictionary(modelData.key)
                        }
                        Button {
                            visible: modelData.installed
                            text: qsTr("Remove")
                            onClicked: settings.app.removeDictionary(modelData.key)
                        }
                    }
                }
            }

            // Hear Pronunciation and Listen, with the phone's own voices (the desktop's Settings › Audio).
            Section { text: qsTr("AUDIO") }
            Label {
                text: qsTr("Speed: %1").arg(speedSlider.value === 0 ? qsTr("Normal") : (speedSlider.value > 0 ? "+" : "") + Math.round(speedSlider.value) + " %")
                color: settings.theme.fg
            }
            Slider {
                id: speedSlider
                objectName: "optSpeechRate"
                Layout.fillWidth: true
                from: -100; to: 100; stepSize: 10
                value: settings.app.options.speechRate || 0
                onMoved: settings.app.setOption("speechRate", Math.round(value))
            }
            Label { text: qsTr("Volume: %1 %").arg(Math.round(volumeSlider.value)); color: settings.theme.fg }
            Slider {
                id: volumeSlider
                objectName: "optSpeechVolume"
                Layout.fillWidth: true
                from: 0; to: 100; stepSize: 5
                value: settings.app.options.speechVolume === undefined ? 100 : settings.app.options.speechVolume
                onMoved: settings.app.setOption("speechVolume", Math.round(value))
            }
            Repeater {
                // A voice for each language (the phone's choice unless one is picked); Test says a sentence with it.
                model: settings.app.voiceLanguages()
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: qsTr("%1 Voice").arg(modelData.name); color: settings.theme.fg; Layout.preferredWidth: 110; wrapMode: Text.Wrap }
                    ComboBox {
                        objectName: "voice-" + modelData.code
                        Layout.fillWidth: true
                        textRole: "detail"
                        valueRole: "name"
                        model: settings.app.voicesFor(modelData.code)
                        currentIndex: { const v = settings.app.voiceFor(modelData.code); for (let i = 0; i < count; ++i) if (model[i].name === v) return i; return 0 }
                        onActivated: (i) => settings.app.setVoice(modelData.code, model[i].name)
                    }
                    Button { objectName: "testVoice-" + modelData.code; text: qsTr("Test"); onClicked: settings.app.testVoice(modelData.code) }
                }
            }
            Note { text: qsTr("More voices (free): the phone's Settings › General management › Text-to-speech.") }

            Section { text: qsTr("ABOUT") }
            Note { text: qsTr("Biblioglot %1 · © cincottash · for DRM-free books.").arg(settings.app.version) }
        }
    }
}

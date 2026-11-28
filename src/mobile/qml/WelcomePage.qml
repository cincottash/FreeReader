// The first start (as on the desktop): your language (the app's text and every answer in it) and the dictionaries to
// download for reading without the internet. Both can be changed later in Settings.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Popup {
    id: welcome
    property MobileApp app
    property Theme theme
    property string language: "en"
    property var picked: ({})   // dictionary key -> on

    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 400
    height: parent ? parent.height : 800
    modal: true
    closePolicy: Popup.NoAutoClose
    padding: 0
    topPadding: app ? app.safeTop : 0
    bottomPadding: app ? app.safeBottom : 0
    background: Rectangle { color: welcome.theme.bg }
    onAboutToShow: { language = app.suggestedLanguage(); fill() }
    function fill() {
        const p = {}
        for (const d of app.dictionariesFor(language)) p[d.key] = d.suggested && !d.installed
        picked = p
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 48
        clip: true
        ColumnLayout {
            id: col
            x: 24
            y: 32
            width: parent.width - 48
            spacing: 12
            Image {
                Layout.alignment: Qt.AlignHCenter
                source: "qrc:/icon.svg"
                sourceSize.width: 72
                sourceSize.height: 72
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: welcome.language === "es" ? "Bienvenido a Biblioglot" : "Welcome to Biblioglot"
                font.pixelSize: 24
                font.weight: Font.DemiBold
                color: welcome.theme.fg
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: welcome.theme.muted
                text: welcome.language === "es"
                      ? "Un lector de libros para leer en otro idioma: mantén pulsada una palabra y su significado en esa frase está a un toque."
                      : "A book reader for reading in another language: press and hold a word and its meaning in that sentence is a tap away."
            }
            // The language: each named in itself, so anyone can find theirs.
            Label {
                Layout.topMargin: 12
                text: welcome.language === "es" ? "TU IDIOMA" : "YOUR LANGUAGE"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.letterSpacing: 1
                color: welcome.theme.muted
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: welcome.app ? welcome.app.readerLanguages() : []
                    delegate: Button {
                        required property var modelData
                        objectName: "welcomeLanguage-" + modelData.code
                        Layout.fillWidth: true
                        text: modelData.name
                        highlighted: welcome.language === modelData.code
                        onClicked: { welcome.language = modelData.code; welcome.fill() }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: welcome.theme.small
                color: welcome.theme.muted
                text: welcome.language === "es" ? "El texto de la app, y el idioma de las traducciones y explicaciones."
                                                : "The app's text, and the language of the translations and explanations."
            }
            Label {
                Layout.topMargin: 12
                text: welcome.language === "es" ? "DICCIONARIOS SIN CONEXIÓN (OPCIONAL)" : "OFFLINE DICTIONARIES (OPTIONAL)"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.letterSpacing: 1
                color: welcome.theme.muted
            }
            Repeater {
                model: welcome.app ? welcome.app.dictionariesFor(welcome.language) : []
                delegate: CheckBox {
                    required property var modelData
                    objectName: "welcomeDictionary-" + modelData.key
                    Layout.fillWidth: true
                    enabled: !modelData.installed
                    checked: modelData.installed || welcome.picked[modelData.key] === true
                    text: modelData.title + "  ·  " + (modelData.installed ? (welcome.language === "es" ? "instalado" : "installed") : modelData.sizeMb + " MB")
                    onToggled: { const p = welcome.picked; p[modelData.key] = checked; welcome.picked = p }
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: welcome.theme.small
                color: welcome.theme.muted
                text: welcome.language === "es"
                      ? "Funcionan sin internet. Gemini (en línea, gratis con una clave) se añade en Ajustes."
                      : "They work with no internet. Gemini (online, free with a key) is added in Settings."
            }
            Button {
                objectName: "welcomeStart"
                Layout.fillWidth: true
                Layout.topMargin: 16
                highlighted: true
                text: welcome.language === "es" ? "Empezar" : "Start"
                onClicked: {
                    const keys = []
                    for (const k in welcome.picked) if (welcome.picked[k]) keys.push(k)
                    welcome.app.finishWelcome(welcome.language, keys)
                    welcome.close()
                }
            }
        }
    }
}

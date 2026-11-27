// A note on the selected words (they're highlighted in yellow when they aren't yet).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Popup {
    id: editor
    property MobileApp app
    property Theme theme
    property BookPage page
    property string quote: ""

    function edit() {
        quote = page.selectedText
        field.text = app.noteAt(page)
        open()
        field.forceActiveFocus()
    }

    modal: true
    x: 12
    y: 70
    width: (parent ? parent.width : 400) - 24
    padding: 16
    background: Rectangle { color: editor.theme.pop; radius: 12; border.color: editor.theme.line }

    ColumnLayout {
        width: parent.width
        spacing: 10
        Label {
            Layout.fillWidth: true
            text: "“" + editor.quote + "”"
            maximumLineCount: 3
            elide: Text.ElideRight
            wrapMode: Text.Wrap
            font.family: editor.theme.serif
            font.italic: true
            color: editor.theme.muted
        }
        TextArea {
            id: field
            objectName: "noteField"
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            wrapMode: TextEdit.Wrap
            placeholderText: qsTr("Your Note")
            color: editor.theme.fg
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button { text: qsTr("Cancel"); flat: true; onClicked: editor.close() }
            Button {
                text: qsTr("Save")
                highlighted: true
                onClicked: {
                    editor.app.setNote(editor.page, field.text)
                    editor.close()
                }
            }
        }
    }
}

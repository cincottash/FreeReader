// The Notebook's export (share it through the phone's share sheet, or save it to a file, in Markdown, plain text,
// CSV, TSV or JSON) and Import Highlights (a Kindle export or a FreeReader Notebook export; what would come in is
// shown first, with the desktop's choices).
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FreeReaderMobile

Item {
    id: tools
    property MobileApp app
    property Theme theme

    function exportNotebook() { exportDialog.open() }
    function importHighlights() { importPicker.open() }

    readonly property var formats: [
        { key: "markdown", name: qsTr("Markdown") }, { key: "text", name: qsTr("Plain Text") },
        { key: "csv", name: qsTr("CSV (Spreadsheet)") }, { key: "tsv", name: qsTr("TSV (Spreadsheet)") },
        { key: "json", name: qsTr("JSON") }]

    Dialog {
        id: exportDialog
        objectName: "exportDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min((parent ? parent.width : 400) - 48, 400)
        modal: true
        title: qsTr("Export the Notebook")
        ColumnLayout {
            width: parent.width
            spacing: 8
            Label { text: qsTr("Format"); color: tools.theme.muted; font.pixelSize: tools.theme.small }
            ComboBox {
                id: formatBox
                objectName: "exportFormat"
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "key"
                model: tools.formats
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: tools.theme.muted
                text: qsTr("Every format can be read back with Import Highlights, here or on the desktop.")
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button {
                objectName: "exportSave"
                text: qsTr("Save to File…")
                flat: true
                Layout.bottomMargin: 12
                onClicked: { saver.currentFile = "file:///" + tools.app.notebookFileName(formatBox.currentValue); saver.open() }
            }
            Button {
                objectName: "exportShare"
                text: qsTr("Share…")
                highlighted: true
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                onClicked: { tools.app.shareNotebook(formatBox.currentValue); exportDialog.close() }
            }
        }
    }
    FileDialog {
        id: saver
        title: qsTr("Save the Notebook")
        fileMode: FileDialog.SaveFile
        onAccepted: { tools.app.saveNotebook(formatBox.currentValue, selectedFile); exportDialog.close() }
    }

    FileDialog {
        id: importPicker
        title: qsTr("Import Highlights")
        nameFilters: [qsTr("Highlights (*.json *.txt *.md *.csv *.tsv)"), qsTr("All Files (*)")]
        onAccepted: tools.app.readHighlights(selectedFile)
    }
    Dialog {
        id: importDialog
        objectName: "highlightImportDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: (parent ? parent.width : 400) - 24
        height: Math.min(implicitHeight, (parent ? parent.height : 800) - 48)
        modal: true
        readonly property var data_: tools.app ? tools.app.highlightImport : ({})
        readonly property bool done: (data_.result || "").length > 0
        title: data_.title || ""
        onClosed: tools.app.closeHighlightImport()
        Connections {
            target: tools.app
            function onHighlightImportChanged() {
                if ((tools.app.highlightImport.title || "").length > 0) { if (!importDialog.opened) importDialog.open() }
                else importDialog.close()
            }
        }
        // The choices as they are when Import is tapped (key -> on).
        property var choices: ({})
        Flickable {
            anchors.fill: parent
            implicitHeight: importCol.implicitHeight
            implicitWidth: importCol.implicitWidth
            contentHeight: importCol.implicitHeight
            clip: true
            ColumnLayout {
                id: importCol
                width: parent.width
                spacing: 6
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: importDialog.data_.from || ""; font.weight: Font.DemiBold; color: tools.theme.fg }
                Repeater {
                    model: importDialog.data_.summary || []
                    delegate: Label { required property string modelData; Layout.fillWidth: true; wrapMode: Text.Wrap; text: "• " + modelData; color: tools.theme.fg }
                }
                Label {
                    objectName: "importWarning"
                    Layout.fillWidth: true
                    visible: text.length > 0 && !importDialog.done
                    wrapMode: Text.Wrap
                    text: importDialog.data_.warning || ""
                    color: "#C98A2E"
                }
                Repeater {
                    model: importDialog.done ? [] : (importDialog.data_.options || [])
                    delegate: CheckBox {
                        required property var modelData
                        objectName: "import-" + modelData.key
                        Layout.fillWidth: true
                        visible: modelData.visible
                        enabled: modelData.enabled
                        checked: modelData.on
                        text: modelData.label
                        contentItem: Label { leftPadding: parent.indicator.width + 8; text: parent.text; wrapMode: Text.Wrap; color: tools.theme.fg; opacity: parent.enabled ? 1 : 0.5 }
                        Component.onCompleted: importDialog.choices[modelData.key] = checked
                        onToggled: importDialog.choices[modelData.key] = checked
                    }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    visible: !importDialog.done && (importDialog.data_.mismatches || []).length > 0
                    text: qsTr("Not in This Book's Text")
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: tools.theme.muted
                }
                Repeater {
                    model: importDialog.done ? [] : (importDialog.data_.mismatches || [])
                    delegate: Label { required property string modelData; Layout.fillWidth: true; wrapMode: Text.Wrap; text: modelData; font.pixelSize: 12; color: tools.theme.muted }
                }
                Label {
                    objectName: "importResult"
                    Layout.fillWidth: true
                    visible: importDialog.done
                    wrapMode: Text.Wrap
                    text: importDialog.data_.result || ""
                    color: tools.theme.fg
                }
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button { text: importDialog.done ? qsTr("Close") : qsTr("Cancel"); flat: !importDialog.done; Layout.bottomMargin: 12; Layout.rightMargin: importDialog.done ? 12 : 0; onClicked: importDialog.close() }
            Button {
                objectName: "importGo"
                visible: !importDialog.done
                text: qsTr("Import")
                highlighted: true
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                onClicked: tools.app.importHighlights(importDialog.choices)
            }
        }
    }
}

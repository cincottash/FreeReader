// Search This Book or Search All Books (as on the desktop): type, then tap a match to go there (the matches stay for
// the next one). Whole Words, Match Case and Match Accents as on the desktop ("razon" also finds "razón" unless Match
// Accents is on). From the library only All Books is there.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Drawer {
    id: sheet
    property MobileApp app
    property Theme theme
    property bool allBooks: !(app && app.reading)   // the library has no open book: every book
    readonly property bool canPickScope: app ? app.reading : false

    edge: Qt.TopEdge
    // Opened by its button only: a swipe down from the top is the phone's own (and the library's sheet lies under
    // the reader); once open, a swipe up closes it.
    interactive: opened
    topPadding: app ? app.safeTop : 0
    width: parent ? parent.width : 400
    height: (parent ? parent.height : 800) * 0.8
    background: Rectangle { color: sheet.theme.surface }
    onAboutToShow: if (!canPickScope) allBooks = true
    onOpened: field.forceActiveFocus()
    onClosed: if (app) app.cancelSearchAll()

    function run() {
        if (sheet.allBooks) sheet.app.searchAll(field.text, whole.checked, matchCase.checked, accents.checked)
        else sheet.app.search(field.text, whole.checked, matchCase.checked, accents.checked)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        // This Book or All Books, while reading.
        RowLayout {
            Layout.fillWidth: true
            visible: sheet.canPickScope
            spacing: 8
            Repeater {
                model: [qsTr("This Book"), qsTr("All Books")]
                delegate: Button {
                    required property string modelData
                    required property int index
                    objectName: index === 0 ? "scopeThisBook" : "scopeAllBooks"
                    Layout.fillWidth: true
                    text: modelData
                    checkable: true
                    checked: (index === 1) === sheet.allBooks
                    highlighted: checked
                    onClicked: { sheet.allBooks = index === 1; if (field.text.trim().length >= 2) sheet.run() }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: field
                objectName: "searchField"
                Layout.fillWidth: true
                placeholderText: sheet.allBooks ? qsTr("Search All Books") : qsTr("Search This Book")
                inputMethodHints: Qt.ImhNoPredictiveText
                color: sheet.theme.fg
                onAccepted: sheet.run()
            }
            IconButton {
                objectName: "searchGo"
                theme: sheet.theme
                iconName: sheet.allBooks && sheet.app && sheet.app.searchingAll ? "stop" : "search"
                text: sheet.allBooks && sheet.app && sheet.app.searchingAll ? qsTr("Stop") : qsTr("Search")
                onClicked: if (sheet.allBooks && sheet.app.searchingAll) sheet.app.cancelSearchAll(); else sheet.run()
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 4
            CheckBox { id: whole; objectName: "searchWhole"; text: qsTr("Whole Words") }
            CheckBox { id: matchCase; objectName: "searchCase"; text: qsTr("Match Case") }
            CheckBox { id: accents; objectName: "searchAccents"; text: qsTr("Match Accents") }
        }
        Label {
            objectName: "searchSummary"
            Layout.fillWidth: true
            text: sheet.app ? (sheet.allBooks ? sheet.app.allSummary : sheet.app.searchSummary) : ""
            wrapMode: Text.Wrap
            color: sheet.theme.muted
            font.pixelSize: sheet.theme.small
        }
        // This book's matches.
        ListView {
            objectName: "searchResults"
            visible: !sheet.allBooks
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
                        text: sheet.marked(modelData)
                    }
                    Label { text: modelData.page ? qsTr("Page %1").arg(modelData.page) : ""; font.pixelSize: 12; color: sheet.theme.muted }
                }
                onClicked: { sheet.app.goToSearchResult(index); sheet.close() }
            }
        }
        // Every book's matches, under the book's title.
        ListView {
            objectName: "allResults"
            visible: sheet.allBooks
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: sheet.app ? sheet.app.allResults : []
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: ListView.view.width
                enabled: modelData.kind === "hit"
                topPadding: modelData.kind === "book" ? 14 : 6
                contentItem: Text {
                    textFormat: modelData.kind === "book" ? Text.PlainText : Text.StyledText
                    wrapMode: Text.Wrap
                    font.family: modelData.kind === "book" ? sheet.font.family : sheet.theme.serif
                    font.pixelSize: modelData.kind === "book" ? sheet.theme.small : sheet.theme.body
                    font.weight: modelData.kind === "book" ? Font.DemiBold : Font.Normal
                    color: modelData.kind === "book" ? sheet.theme.accent : sheet.theme.fg
                    text: modelData.kind === "book"
                          ? modelData.title + (modelData.author ? " · " + modelData.author : "") + "  (" + modelData.count + ")"
                          : sheet.marked(modelData)
                }
                onClicked: { sheet.app.openAllResult(index); sheet.close() }
            }
        }
    }

    // "…before <b>match</b> after…", the book's text escaped.
    function marked(m) {
        const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;")
        return "…" + esc(m.before) + "<b>" + esc(m.match) + "</b>" + esc(m.after) + "…"
    }
}

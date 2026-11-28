// The library (as on the desktop): the books on this phone, as covers or a list; Search Title or Author; filters
// (Reading, Finished, Not Started, a language, a collection); the order; Continue Reading. Add Book picks a book
// (EPUB, Kindle, FB2, comics and more); tap a book to read it; press and hold for its menu (collections, Mark as
// Finished, Locate File for one that is gone, Remove).
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FreeReaderMobile

Page {
    id: page
    property MobileApp app
    property Theme theme
    signal settingsRequested()
    signal wordsRequested()
    signal statisticsRequested()
    signal guideRequested()

    background: Rectangle { color: page.theme.bg }

    Dialog {
        id: aboutDialog
        objectName: "aboutDialog"
        anchors.centerIn: parent
        width: page.width - 48
        modal: true
        title: qsTr("About Biblioglot")
        ColumnLayout {
            width: parent.width
            spacing: 8
            Label {
                objectName: "aboutText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: page.theme.fg
                text: qsTr("Biblioglot %1\nAn e-book reader for DRM-free books.\n\nCopyright (C) 2026 cincottash\nLicensed under the GNU General Public License v3.0.").arg(page.app.version)
            }
            Button { Layout.fillWidth: true; flat: true; text: qsTr("Privacy Policy"); onClicked: page.app.openLink("https://cincottash.github.io/Biblioglot/privacy.html") }
            Button { Layout.fillWidth: true; flat: true; text: qsTr("Report a Problem"); onClicked: page.app.openLink("https://github.com/cincottash/Biblioglot/issues/new") }
        }
        standardButtons: Dialog.Close
    }

    // The filter's name ("All Books", "Reading", "Spanish", a collection's name).
    readonly property string filterName: {
        const list = page.app ? page.app.libraryFilters : []
        for (let i = 0; i < list.length; ++i) if (list[i].key === page.app.libraryFilter) return list[i].name
        return qsTr("All Books")
    }

    header: Rectangle {
        height: 56
        color: page.theme.surface
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 4
            Label {
                text: "Biblioglot"
                font.pixelSize: page.theme.title
                font.weight: Font.DemiBold
                color: page.theme.fg
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            IconButton {
                objectName: "searchAllButton"
                theme: page.theme
                iconName: "search"
                text: qsTr("Search All Books")
                visible: page.app.bookCount > 0
                onClicked: searchAll.open()
            }
            // Go to Latest Bookmark (as on the desktop's library): the bookmark added last, in any book.
            IconButton {
                objectName: "latestBookmarkButton"
                theme: page.theme
                iconName: "bookmark"
                text: qsTr("Go to Latest Bookmark")
                visible: page.app.bookCount > 0
                onClicked: page.app.goToLatestBookmark()
            }
            ToolButton {
                id: wordsButton
                objectName: "wordsButton"
                text: qsTr("Words")
                font.pixelSize: page.theme.body
                implicitHeight: page.theme.touch
                contentItem: Label { text: wordsButton.text; color: page.theme.accent; font: wordsButton.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Item {}
                onClicked: page.wordsRequested()
            }
            // Settings, Statistics, Getting Started, About.
            IconButton {
                id: moreButton
                objectName: "moreButton"
                theme: page.theme
                iconName: "more"
                filled: true
                text: qsTr("More")
                onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
            }
            Menu {
                id: moreMenu
                objectName: "moreMenu"
                MenuItem { objectName: "menuSettings"; text: qsTr("Settings"); onTriggered: page.settingsRequested() }
                MenuItem { objectName: "menuStatistics"; text: qsTr("Statistics"); onTriggered: page.statisticsRequested() }
                MenuItem { objectName: "menuGuide"; text: qsTr("Getting Started"); onTriggered: page.guideRequested() }
                MenuItem { objectName: "menuAbout"; text: qsTr("About Biblioglot"); onTriggered: aboutDialog.open() }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: page.theme.line }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: page.app.bookCount > 0

        // Search, the filter, the order, covers or list.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 4
            Layout.topMargin: 8
            spacing: 4
            TextField {
                id: libSearch
                objectName: "librarySearch"
                Layout.fillWidth: true
                placeholderText: qsTr("Search Title or Author")
                inputMethodHints: Qt.ImhNoPredictiveText
                color: page.theme.fg
                text: page.app.librarySearch
                onTextEdited: page.app.librarySearch = text
            }
            IconButton {
                objectName: "libraryViewButton"
                theme: page.theme
                iconName: page.app.libraryList ? "grid" : "list"
                text: page.app.libraryList ? qsTr("Show Covers") : qsTr("Show List")
                onClicked: page.app.libraryList = !page.app.libraryList
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            spacing: 8
            Button {
                objectName: "filterButton"
                Layout.fillWidth: true
                flat: true
                font.pixelSize: page.theme.small
                text: page.filterName + "  ▾"
                onClicked: filters.open()
            }
            ComboBox {
                id: sortBox
                objectName: "librarySort"
                Layout.preferredWidth: 196   // "Leídos recientemente" in full
                font.pixelSize: page.theme.small
                textRole: "name"
                valueRole: "key"
                model: page.app ? page.app.librarySorts() : []
                currentIndex: { const k = page.app.librarySort; for (let i = 0; i < count; ++i) if (model[i].key === k) return i; return 0 }
                onActivated: (i) => page.app.librarySort = model[i].key
            }
        }

        // Continue Reading: the book read last that isn't finished.
        Rectangle {
            objectName: "continueBar"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 4
            visible: (page.app.continueReading.id || 0) > 0 && page.app.libraryFilter === "all" && page.app.librarySearch.length === 0
            implicitHeight: continueRow.implicitHeight + 16
            radius: 8
            color: page.theme.surface
            border.color: page.theme.line
            RowLayout {
                id: continueRow
                anchors.fill: parent
                anchors.margins: 8
                anchors.leftMargin: 12
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label { text: qsTr("Continue Reading"); font.pixelSize: 12; color: page.theme.muted }
                    Label {
                        Layout.fillWidth: true
                        text: page.app.continueReading.title || ""
                        elide: Text.ElideRight
                        font.pixelSize: page.theme.body
                        font.weight: Font.DemiBold
                        color: page.theme.fg
                    }
                    Label {
                        Layout.fillWidth: true
                        text: [page.app.continueReading.page || "", page.app.continueReading.percent || ""].filter((s) => s.length > 0).join(" · ")
                        font.pixelSize: 12
                        color: page.theme.muted
                    }
                }
                Button {
                    objectName: "continueButton"
                    text: qsTr("Continue")
                    highlighted: true
                    onClicked: page.app.openBook(page.app.continueReading.id)
                }
            }
        }

        // The covers.
        GridView {
            id: grid
            objectName: "libraryGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            clip: true
            visible: !page.app.libraryList && page.app.shownBookCount > 0
            model: page.app.books
            readonly property int columns: Math.max(2, Math.floor(width / 150))
            cellWidth: Math.floor(width / columns)
            cellHeight: cellWidth * 1.5 + 64
            delegate: Item {
                id: tile
                required property int bookId
                required property string title
                required property string author
                required property double progress
                required property bool hasCover
                required property bool finished
                required property bool missing
                width: grid.cellWidth
                height: grid.cellHeight
                Column {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    Rectangle {
                        width: parent.width
                        height: width * 1.5
                        radius: 4
                        color: Qt.hsla((tile.title.length * 37 % 360) / 360, 0.35, page.theme.dark ? 0.3 : 0.7, 1)
                        border.color: page.theme.line
                        clip: true
                        opacity: tile.missing ? 0.45 : 1
                        Image {
                            anchors.fill: parent
                            visible: tile.hasCover
                            source: tile.hasCover ? "image://covers/" + tile.bookId : ""
                            fillMode: Image.PreserveAspectFit
                            sourceSize.height: 400
                        }
                        Label {
                            visible: !tile.hasCover
                            anchors.fill: parent
                            anchors.margins: 12
                            text: tile.title
                            wrapMode: Text.Wrap
                            font.family: page.theme.serif
                            font.pixelSize: 16
                            color: page.theme.fg
                        }
                    }
                    Label {
                        width: parent.width
                        text: tile.title
                        elide: Text.ElideRight
                        font.pixelSize: page.theme.small
                        font.weight: Font.DemiBold
                        color: page.theme.fg
                    }
                    Label {
                        width: parent.width
                        text: tile.missing ? qsTr("File Not Found")
                              : tile.finished ? qsTr("Finished") + (tile.author ? "  ·  " + tile.author : "")
                              : tile.progress > 0.0005 ? Math.round(tile.progress * 100) + " %  ·  " + tile.author : tile.author
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        color: tile.missing ? "#C0392B" : page.theme.muted
                    }
                }
                TapHandler {
                    onTapped: tile.missing ? page.showMenu(tile.bookId, tile.title) : page.app.openBook(tile.bookId)
                    onLongPressed: page.showMenu(tile.bookId, tile.title)
                }
            }
        }

        // The list.
        ListView {
            id: list
            objectName: "libraryList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            visible: page.app.libraryList && page.app.shownBookCount > 0
            model: page.app.books
            delegate: ItemDelegate {
                id: row
                required property int bookId
                required property string title
                required property string author
                required property double progress
                required property bool hasCover
                required property bool finished
                required property bool missing
                required property string language
                width: ListView.view.width
                height: 76
                contentItem: RowLayout {
                    spacing: 12
                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 60
                        radius: 3
                        color: Qt.hsla((row.title.length * 37 % 360) / 360, 0.35, page.theme.dark ? 0.3 : 0.7, 1)
                        border.color: page.theme.line
                        clip: true
                        opacity: row.missing ? 0.45 : 1
                        Image {
                            anchors.fill: parent
                            visible: row.hasCover
                            source: row.hasCover ? "image://covers/" + row.bookId : ""
                            fillMode: Image.PreserveAspectFit
                            sourceSize.height: 120
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { Layout.fillWidth: true; text: row.title; elide: Text.ElideRight; font.pixelSize: page.theme.body; font.weight: Font.DemiBold; color: page.theme.fg }
                        Label { Layout.fillWidth: true; text: row.author; elide: Text.ElideRight; font.pixelSize: page.theme.small; color: page.theme.muted }
                        Label {
                            Layout.fillWidth: true
                            text: row.missing ? qsTr("File Not Found") : row.finished ? qsTr("Finished")
                                  : row.progress > 0.0005 ? Math.round(row.progress * 100) + " %" : qsTr("Not Started")
                            font.pixelSize: 12
                            color: row.missing ? "#C0392B" : page.theme.muted
                        }
                    }
                }
                background: Rectangle { color: row.pressed ? Qt.rgba(0.5, 0.5, 0.5, 0.15) : "transparent" }
                onClicked: row.missing ? page.showMenu(row.bookId, row.title) : page.app.openBook(row.bookId)
                onPressAndHold: page.showMenu(row.bookId, row.title)
            }
        }

        Label {
            objectName: "noBooksMatch"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 32
            visible: page.app.shownBookCount === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignTop
            wrapMode: Text.Wrap
            text: qsTr("No Books Match")
            font.pixelSize: page.theme.body
            color: page.theme.muted
        }
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 64
        visible: page.app.bookCount === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: qsTr("No Books Yet") + "\n" + qsTr("Tap Add Book to choose a book on this phone (EPUB, MOBI, AZW3, FB2, comics, text and more). Books with DRM can't be opened.")
        font.pixelSize: page.theme.body
        color: page.theme.muted
    }

    // ---- a book's menu
    property int menuBookId: 0
    property string menuBookTitle: ""
    function showMenu(id, title) {
        menuBookId = id
        menuBookTitle = title
        bookMenu.missing = page.app.isMissing(id)
        bookMenu.finished = page.app.isFinished(id)
        bookMenu.popup()
    }
    Menu {
        id: bookMenu
        objectName: "bookMenu"
        property bool missing: false
        property bool finished: false
        MenuItem { text: qsTr("Open"); visible: !bookMenu.missing; height: visible ? implicitHeight : 0; onTriggered: page.app.openBook(page.menuBookId) }
        MenuItem { objectName: "menuLocate"; text: qsTr("Locate File…"); visible: bookMenu.missing; height: visible ? implicitHeight : 0; onTriggered: locatePicker.open() }
        MenuItem { objectName: "menuCollections"; text: qsTr("Collections…"); onTriggered: collectionsDialog.open() }
        MenuItem {
            objectName: "menuFinished"
            text: bookMenu.finished ? qsTr("Mark as Not Finished") : qsTr("Mark as Finished")
            onTriggered: page.app.setFinished(page.menuBookId, !bookMenu.finished)
        }
        MenuItem { text: qsTr("Remove from Library"); onTriggered: removeDialog.open() }
    }
    Dialog {
        id: removeDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Remove “%1”?").arg(page.menuBookTitle)
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label { text: qsTr("The book and its place are removed from this phone. Its words stay in the Word List."); wrapMode: Text.Wrap; width: page.width - 96 }
        onAccepted: page.app.removeBook(page.menuBookId)
    }

    // The book's collections: check the ones it is in; a new one by name.
    Dialog {
        id: collectionsDialog
        objectName: "collectionsDialog"
        anchors.centerIn: parent
        width: page.width - 48
        modal: true
        title: qsTr("Collections")
        property var rows: []
        onAboutToShow: { rows = page.app.collectionsFor(page.menuBookId); newName.text = ""; newError.text = "" }
        ColumnLayout {
            width: parent.width
            spacing: 4
            Repeater {
                model: collectionsDialog.rows
                delegate: CheckBox {
                    required property var modelData
                    objectName: "collection-" + modelData.name
                    Layout.fillWidth: true
                    text: modelData.name
                    checked: modelData.has
                    onToggled: page.app.setInCollection(modelData.id, page.menuBookId, checked)
                }
            }
            Label {
                visible: collectionsDialog.rows.length === 0
                text: qsTr("No Collections Yet")
                color: page.theme.muted
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: newName
                    objectName: "newCollectionName"
                    Layout.fillWidth: true
                    placeholderText: qsTr("New Collection")
                    color: page.theme.fg
                    onAccepted: addCollection.clicked()
                }
                Button {
                    id: addCollection
                    objectName: "newCollectionAdd"
                    text: qsTr("Add")
                    onClicked: {
                        const problem = page.app.createCollection(newName.text, page.menuBookId)
                        newError.text = problem
                        if (problem.length === 0) { newName.text = ""; collectionsDialog.rows = page.app.collectionsFor(page.menuBookId) }
                    }
                }
            }
            Label { id: newError; Layout.fillWidth: true; visible: text.length > 0; wrapMode: Text.Wrap; color: page.theme.accent; font.pixelSize: page.theme.small }
        }
        standardButtons: Dialog.Close
    }

    // The filters: Library, Languages, Collections (press and hold a collection to rename or delete it).
    Drawer {
        id: filters
        objectName: "filterDrawer"
        edge: Qt.LeftEdge
        interactive: opened
        topPadding: page.app ? page.app.safeTop : 0
        width: Math.min(320, page.width * 0.85)
        height: page.height
        background: Rectangle { color: page.theme.surface }
        ListView {
            id: filterList
            anchors.fill: parent
            clip: true
            model: page.app ? page.app.libraryFilters : []
            delegate: Column {
                id: entry
                required property var modelData
                required property int index
                width: ListView.view.width
                Label {
                    visible: entry.index === 0 || filterList.model[entry.index - 1].group !== entry.modelData.group
                    width: parent.width
                    leftPadding: 16
                    topPadding: 14
                    bottomPadding: 4
                    text: entry.modelData.group.toUpperCase()
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                    color: page.theme.muted
                }
                ItemDelegate {
                    objectName: "filter-" + entry.modelData.key
                    width: parent.width
                    height: 46
                    leftPadding: 16
                    contentItem: RowLayout {
                        Label {
                            Layout.fillWidth: true
                            text: entry.modelData.name
                            elide: Text.ElideRight
                            color: page.app.libraryFilter === entry.modelData.key ? page.theme.accent : page.theme.fg
                            font.weight: page.app.libraryFilter === entry.modelData.key ? Font.DemiBold : Font.Normal
                        }
                        Label { text: entry.modelData.count; color: page.theme.muted; font.pixelSize: page.theme.small }
                    }
                    onClicked: { page.app.libraryFilter = entry.modelData.key; filters.close() }
                    onPressAndHold: if (entry.modelData.key.startsWith("collection:")) {
                        collectionEdit.collectionId = parseInt(entry.modelData.key.substring(11))
                        collectionEdit.oldName = entry.modelData.name
                        collectionEdit.open()
                    }
                }
            }
            footer: ItemDelegate {
                objectName: "filterNewCollection"
                width: ListView.view.width
                leftPadding: 16
                text: qsTr("+ New Collection")
                onClicked: { collectionEdit.collectionId = 0; collectionEdit.oldName = ""; collectionEdit.open() }
            }
        }
    }
    // New Collection, or Rename / Delete one.
    Dialog {
        id: collectionEdit
        objectName: "collectionEdit"
        property int collectionId: 0
        property string oldName: ""
        anchors.centerIn: parent
        width: page.width - 48
        modal: true
        title: collectionId > 0 ? qsTr("Rename Collection") : qsTr("New Collection")
        onAboutToShow: { editName.text = oldName; editError.text = "" }
        ColumnLayout {
            width: parent.width
            TextField { id: editName; objectName: "collectionName"; Layout.fillWidth: true; placeholderText: qsTr("Collection Name"); color: page.theme.fg }
            Label { id: editError; Layout.fillWidth: true; visible: text.length > 0; wrapMode: Text.Wrap; color: page.theme.accent; font.pixelSize: page.theme.small }
        }
        footer: RowLayout {
            spacing: 8
            Button {
                objectName: "collectionDelete"
                visible: collectionEdit.collectionId > 0
                Layout.leftMargin: 12
                Layout.bottomMargin: 12
                flat: true
                text: qsTr("Delete Collection")
                onClicked: { page.app.deleteCollection(collectionEdit.collectionId); collectionEdit.close() }
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Cancel"); flat: true; Layout.bottomMargin: 12; onClicked: collectionEdit.close() }
            Button {
                objectName: "collectionSave"
                text: collectionEdit.collectionId > 0 ? qsTr("Rename") : qsTr("Create")
                highlighted: true
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                onClicked: {
                    const problem = collectionEdit.collectionId > 0 ? page.app.renameCollection(collectionEdit.collectionId, editName.text)
                                                                    : page.app.createCollection(editName.text)
                    editError.text = problem
                    if (problem.length === 0) collectionEdit.close()
                }
            }
        }
    }

    SearchSheet { id: searchAll; objectName: "librarySearchSheet"; app: page.app; theme: page.theme }

    FileDialog {
        id: picker
        title: qsTr("Add Book")
        nameFilters: page.app ? page.app.bookFilters : [qsTr("All Files (*)")]
        onAccepted: page.app.importBook(selectedFile)
    }
    FileDialog {
        id: locatePicker
        title: qsTr("Locate Book File")
        nameFilters: page.app ? page.app.bookFilters : [qsTr("All Files (*)")]
        onAccepted: page.app.locateBook(page.menuBookId, selectedFile)
    }

    Button {
        id: addButton
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        text: qsTr("Add Book")
        enabled: !page.app.busy
        font.pixelSize: page.theme.body
        font.weight: Font.DemiBold
        implicitHeight: 52
        leftPadding: 24
        rightPadding: 24
        contentItem: Label { text: addButton.text; font: addButton.font; color: page.theme.accentText; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 26; color: page.theme.accent; opacity: addButton.enabled ? 1 : 0.5 }
        onClicked: picker.open()
    }

    // Messages ("Added …", why a book couldn't be added) for a few seconds.
    Rectangle {
        id: toast
        anchors.left: parent.left
        anchors.right: addButton.left
        anchors.bottom: parent.bottom
        anchors.margins: 20
        height: toastText.implicitHeight + 24
        radius: 8
        color: page.theme.pop
        border.color: page.theme.line
        visible: toastText.text.length > 0 && toastTimer.running
        Label {
            id: toastText
            anchors.fill: parent
            anchors.margins: 12
            text: page.app.message
            wrapMode: Text.Wrap
            font.pixelSize: page.theme.small
            color: page.theme.fg
        }
        Timer { id: toastTimer; interval: 6000 }
        Connections {
            target: page.app
            function onMessageChanged() { if (page.app.message.length > 0) toastTimer.restart() }
        }
    }
}

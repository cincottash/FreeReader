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

    component Section: Label {
        Layout.topMargin: 14
        font.pixelSize: 12
        font.weight: Font.DemiBold
        font.letterSpacing: 1
        color: words.theme.muted
    }
    // A practice option: a check box bound to one of the app's options by name.
    component Option: CheckBox {
        id: opt
        property string key
        Layout.fillWidth: true
        checked: words.app.options[key] === true
        onToggled: words.app.setOption(key, checked)
        contentItem: Label {
            leftPadding: opt.indicator.width + opt.spacing
            text: opt.text
            wrapMode: Text.Wrap
            color: words.theme.fg
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle { color: words.theme.bg }

    // A word's actions (a tap on it in the Word List).
    Menu {
        id: wordMenu
        objectName: "wordMenu"
        property int wordId: 0
        property string meaning: ""
        MenuItem {
            objectName: "wordShowInBook"
            text: qsTr("Show in Book")
            // Back to the reader (its book, or the word's book once it opens).
            onTriggered: { words.app.showWordInBook(wordMenu.wordId); words.closeRequested() }
        }
        MenuItem {
            objectName: "wordChangeMeaning"
            text: qsTr("Change Meaning…")
            onTriggered: { meaningField.text = wordMenu.meaning; meaningDialog.open() }
        }
        MenuItem {
            objectName: "wordStartOver"
            text: qsTr("Start Over")
            onTriggered: words.app.startOverWord(wordMenu.wordId)
        }
    }
    Dialog {
        id: meaningDialog
        objectName: "meaningDialog"
        anchors.centerIn: parent
        width: parent.width - 48
        modal: true
        title: qsTr("Change Meaning")
        standardButtons: Dialog.Cancel | Dialog.Save
        TextField { id: meaningField; width: parent.width; placeholderText: qsTr("Meaning") }
        onAccepted: words.app.setWordMeaning(wordMenu.wordId, meaningField.text)
    }
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
            // The start: how many cards, Start, and the desktop's practice options below it.
            Flickable {
                visible: !words.p.active
                anchors.fill: parent
                contentHeight: start.implicitHeight + 48
                clip: true
            ColumnLayout {
                id: start
                x: 24
                y: 24
                width: parent.width - 48
                spacing: 14
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: (words.p.summary || "").length > 0 ? words.p.summary
                          : (words.p.cards === 1 ? qsTr("1 Card to Practice") : qsTr("%1 Cards to Practice").arg(words.p.cards || 0))
                    font.pixelSize: words.theme.body
                    color: words.theme.fg
                }
                // The order the cards come in.
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 10
                    Label { text: qsTr("Order"); color: words.theme.fg }
                    ComboBox {
                        id: order
                        objectName: "practiceOrder"
                        textRole: "label"
                        valueRole: "key"
                        model: [
                            {"key": "random", "label": qsTr("Random")},
                            {"key": "alphabetical", "label": qsTr("Alphabetical")},
                            {"key": "newest", "label": qsTr("Newest First")},
                            {"key": "oldest", "label": qsTr("Oldest First")},
                            {"key": "book", "label": qsTr("Book Order")}
                        ]
                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(words.app.practiceOrder))
                        onActivated: words.app.practiceOrder = currentValue
                    }
                }
                Button {
                    objectName: "startPractice"
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Start Practice")
                    highlighted: true
                    enabled: (words.p.cards || 0) > 0
                    onClicked: words.app.startPractice()
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: (words.p.words || 0) === 0
                    text: qsTr("Words highlighted while reading become flashcards here.")
                    font.pixelSize: words.theme.small
                    color: words.theme.muted
                }

                // ---- The options (the same as the desktop's Practice page).
                Section { text: qsTr("WHAT TO PRACTICE") }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Words From"); color: words.theme.fg }
                    ComboBox {
                        id: fromBook
                        objectName: "practiceBook"
                        Layout.fillWidth: true
                        textRole: "title"
                        valueRole: "id"
                        model: [{"id": 0, "title": qsTr("All Books")}].concat(words.app ? words.app.wordBooks() : [])
                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(words.app.options.practiceBook))
                        onActivated: words.app.setOption("practiceBook", currentValue)
                    }
                }
                Repeater {
                    // The four kinds of card; at least one stays on.
                    model: [{"key": "recognize", "label": qsTr("Book Language → English")},
                            {"key": "recall", "label": qsTr("English → Book Language")},
                            {"key": "definition", "label": qsTr("Definition → Word")},
                            {"key": "cloze", "label": qsTr("Sentence With a Gap → Word")}]
                    delegate: CheckBox {
                        id: dir
                        required property var modelData
                        objectName: "dir-" + modelData.key
                        Layout.fillWidth: true
                        text: modelData.label
                        checked: (words.app.options.practiceDirections || []).indexOf(modelData.key) >= 0
                        onToggled: {
                            let d = (words.app.options.practiceDirections || []).filter(k => k !== modelData.key)
                            if (checked) d.push(modelData.key)
                            words.app.setOption("practiceDirections", d)
                        }
                        contentItem: Label {
                            leftPadding: dir.indicator.width + dir.spacing
                            text: dir.text
                            color: words.theme.fg
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                Option { objectName: "practicePassages"; key: "practicePassages"; text: qsTr("Include Passages (Longer Selections)") }
                Option { objectName: "practiceStarred"; key: "practiceStarredOnly"; text: qsTr("Starred Words Only") }
                Option { objectName: "practiceHighlights"; key: "practiceHighlights"; text: qsTr("Add Highlighted Words to the Word Log") }
                Option { objectName: "practiceLookups"; key: "practiceLookups"; text: qsTr("Add Looked-Up Words to the Word Log") }
                Section { text: qsTr("HOW") }
                Option { objectName: "practiceTyped"; key: "practiceTyped"; text: qsTr("Type the Answer (Instead of Flipping the Card)") }
                Option { objectName: "practiceSentenceFront"; key: "practiceSentenceFront"; text: qsTr("Show the Sentence on the Front") }
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
                        // Type the Answer: how the typed answer went.
                        Label {
                            objectName: "cardVerdict"
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: words.p.revealed === true && (words.p.verdict || "").length > 0
                            text: words.p.verdict || ""
                            font.pixelSize: words.theme.body
                            font.weight: Font.DemiBold
                            color: words.p.suggested >= 3 ? words.theme.accent : words.theme.fg
                        }
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
                // Type the Answer (as on the desktop): a field and Check instead of Show Answer.
                RowLayout {
                    Layout.fillWidth: true
                    visible: words.p.revealed !== true && words.p.typed === true
                    TextField {
                        id: answer
                        objectName: "cardAnswer"
                        Layout.fillWidth: true
                        placeholderText: qsTr("Type the Answer")
                        inputMethodHints: Qt.ImhNoPredictiveText
                        onAccepted: { words.app.checkTyped(text); text = "" }
                    }
                    Button {
                        objectName: "checkAnswer"
                        text: qsTr("Check")
                        highlighted: true
                        onClicked: { words.app.checkTyped(answer.text); answer.text = "" }
                    }
                }
                Button {
                    objectName: "showAnswer"
                    Layout.fillWidth: true
                    visible: words.p.revealed !== true && words.p.typed !== true
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
                            // The rating a typed answer suggests (else Good), highlighted.
                            highlighted: index + 1 === (words.p.suggested > 0 ? words.p.suggested : 3)
                            contentItem: Column {
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData; font.pixelSize: words.theme.body; color: parent.parent.highlighted ? "white" : words.theme.fg }
                            }
                            onClicked: words.app.rateCard(index + 1)
                        }
                    }
                }
            }
        }

        // ---- Word List: search, book and order on top (as on the desktop), then the words. A tap on a word: Show
        // in Book, Change Meaning, Start Over.
        ColumnLayout {
            spacing: 0
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 12
                spacing: 8
                TextField {
                    objectName: "wordSearch"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search Words and Meanings")
                    inputMethodHints: Qt.ImhNoPredictiveText
                    text: words.app.options.wordSearch || ""
                    onTextEdited: words.app.setOption("wordSearch", text)
                }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        objectName: "wordBook"
                        Layout.fillWidth: true
                        textRole: "title"
                        valueRole: "id"
                        model: [{"id": 0, "title": qsTr("All Books")}].concat(words.app ? words.app.wordBooks() : [])
                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(words.app.options.wordBook))
                        onActivated: words.app.setOption("wordBook", currentValue)
                    }
                    ComboBox {
                        objectName: "wordSort"
                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "key"
                        model: [{"key": "recent", "label": qsTr("Recently Looked Up")}, {"key": "newest", "label": qsTr("Newest First")},
                                {"key": "oldest", "label": qsTr("Oldest First")}, {"key": "az", "label": qsTr("A to Z")},
                                {"key": "za", "label": qsTr("Z to A")}, {"key": "meaning", "label": qsTr("Meaning A to Z")},
                                {"key": "meaning-za", "label": qsTr("Meaning Z to A")}, {"key": "book", "label": qsTr("Book Order")},
                                {"key": "book-reverse", "label": qsTr("Book Order, Last First")}, {"key": "lookups", "label": qsTr("Most Looked Up")},
                                {"key": "lookups-least", "label": qsTr("Least Looked Up")}, {"key": "starred", "label": qsTr("Starred First")},
                                {"key": "review", "label": qsTr("Next Review")}, {"key": "review-latest", "label": qsTr("Next Review, Latest First")}]
                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(words.app.options.wordListSort))
                        onActivated: words.app.setOption("wordListSort", currentValue)
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: words.theme.line }
        ListView {
            id: list
            objectName: "wordList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: words.app ? words.app.words : []
            delegate: Item {
                id: entry
                required property var modelData
                width: ListView.view.width
                height: col.implicitHeight + 24
                Rectangle { anchors.fill: parent; color: rowTap.pressed ? Qt.rgba(0.5, 0.5, 0.5, 0.15) : "transparent" }
                MouseArea {
                    id: rowTap
                    anchors.fill: parent
                    onClicked: { wordMenu.wordId = entry.modelData.id; wordMenu.meaning = entry.modelData.translation; wordMenu.popup() }
                }
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
                            text: qsTr("No Meaning Yet")
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
                // The empty list's text in title case (the owner's rule), then how to fill it.
                text: (words.app.options.wordSearch || "").length > 0 ? qsTr("No Matching Words")
                      : qsTr("No Words Yet") + "\n" + qsTr("Press and hold a word while reading, then pick a highlight color.")
                color: words.theme.muted
            }
        }
        }
    }
}

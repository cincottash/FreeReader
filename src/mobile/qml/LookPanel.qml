// Text and Layout (the reader's Aa button), as on the desktop: where the changes go (all books, the book's language,
// this book), the font, text size, line spacing, margins, page width, one or two pages side by side, justification,
// hyphenation; the page-turn animation (all books); the theme.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FreeReaderMobile

Popup {
    id: panel
    property MobileApp app
    property Theme theme
    signal settingsRequested()
    readonly property var look: app ? app.look : ({})

    padding: 0
    background: Rectangle { color: panel.theme.pop; radius: 10; border.color: panel.theme.line }

    component Caption: Label {
        Layout.topMargin: 6
        font.pixelSize: panel.theme.small
        color: panel.theme.muted
    }
    component Choice: Button {
        property string key
        property string current
        Layout.fillWidth: true
        highlighted: key === current
        font.pixelSize: panel.theme.small
    }

    Flickable {
        id: flick
        objectName: "lookFlick"
        anchors.fill: parent
        contentHeight: col.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: col
            x: 16
            y: 16
            width: flick.width - 32
            spacing: 8

            Label { text: qsTr("TEXT & LAYOUT"); font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1; color: panel.theme.muted }
            // Apply To: all books, the book's language, this book.
            Caption { text: qsTr("Apply To") }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Choice { objectName: "scopeAll"; key: "all"; current: panel.look.scope || "all"; text: qsTr("All Books"); onClicked: panel.app.setLookScope("all") }
                Choice {
                    objectName: "scopeLanguage"
                    key: "language"
                    current: panel.look.scope || "all"
                    visible: (panel.look.languageName || "").length > 0
                    text: qsTr("%1 Books").arg(panel.look.languageName || "")
                    onClicked: panel.app.setLookScope("language")
                }
                Choice { objectName: "scopeBook"; key: "book"; current: panel.look.scope || "all"; text: qsTr("This Book"); visible: panel.app && panel.app.reading; onClicked: panel.app.setLookScope("book") }
            }
            Label {
                objectName: "scopeNote"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: panel.theme.muted
                text: panel.look.scope === "book" ? qsTr("Changes apply to this book only.")
                      : panel.look.scope === "language" ? qsTr("Changes apply to %1 only.").arg(qsTr("%1 Books").arg(panel.look.languageName))
                      : qsTr("Changes apply to every book, except where a language or a book has its own setting.")
            }

            Caption { text: qsTr("Font") }
            ComboBox {
                id: fontBox
                objectName: "lookFont"
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "key"
                model: panel.opened && panel.app ? panel.app.fonts() : []
                currentIndex: { const k = panel.look.font || ""; for (let i = 0; i < count; ++i) if (model[i].key === k) return i; return 0 }
                onActivated: (i) => panel.app.setLook("font", model[i].key)
            }

            Caption { text: qsTr("Text Size") }
            RowLayout {
                Layout.fillWidth: true
                Button { objectName: "lookSmaller"; text: "A−"; onClicked: panel.app.textSize = panel.app.textSize - 10 }
                Label { objectName: "lookSize"; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: panel.app.textSize + " %"; color: panel.theme.fg }
                Button { objectName: "lookLarger"; text: "A+"; onClicked: panel.app.textSize = panel.app.textSize + 10 }
            }

            Caption { text: qsTr("Line Spacing: %1 %").arg(spacing.pressed ? Math.round(spacing.value) : (panel.look.line_spacing_pct || 0)) }
            Slider {
                id: spacing
                objectName: "lookSpacing"
                Layout.fillWidth: true
                from: 100; to: 250; stepSize: 5
                value: panel.look.line_spacing_pct || 160
                onPressedChanged: if (!pressed) panel.app.setLook("line_spacing_pct", Math.round(value))
            }

            Caption { text: qsTr("Margins") }
            RowLayout {
                Layout.fillWidth: true
                Button { text: qsTr("Narrower"); onClicked: panel.app.margins = panel.app.margins - 8 }
                Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: panel.app.margins + " px"; color: panel.theme.fg }
                Button { text: qsTr("Wider"); onClicked: panel.app.margins = panel.app.margins + 8 }
            }

            // The longest a line may be: on a tablet the text stays a readable width.
            Caption { text: qsTr("Page Width: %1").arg(width_.pressed ? Math.round(width_.value) : (panel.look.page_width_em || 0)) }
            Slider {
                id: width_
                objectName: "lookPageWidth"
                Layout.fillWidth: true
                from: 16; to: 60; stepSize: 1
                value: panel.look.page_width_em || 33
                onPressedChanged: if (!pressed) panel.app.setLook("page_width_em", Math.round(value))
            }

            // Two pages side by side when the screen is wide enough (a tablet, a phone turned on its side).
            Caption { text: qsTr("Pages Side by Side") }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Choice { objectName: "columnsAuto"; key: "auto"; current: panel.look.columns || "auto"; text: qsTr("Automatic"); onClicked: panel.app.setLook("columns", "auto") }
                Choice { objectName: "columnsOne"; key: "one"; current: panel.look.columns || "auto"; text: qsTr("One"); onClicked: panel.app.setLook("columns", "one") }
                Choice { objectName: "columnsTwo"; key: "two"; current: panel.look.columns || "auto"; text: qsTr("Two"); onClicked: panel.app.setLook("columns", "two") }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: panel.theme.muted
                text: qsTr("Automatic: two pages when the screen is wide enough for both.")
            }

            Switch {
                objectName: "lookJustify"
                Layout.fillWidth: true
                text: qsTr("Justify Text")
                checked: panel.look.justify === true
                onToggled: panel.app.setLook("justify", checked)
            }
            Switch {
                objectName: "lookHyphenate"
                Layout.fillWidth: true
                text: qsTr("Hyphenate")
                checked: panel.look.hyphenate === true
                onToggled: panel.app.setLook("hyphenate", checked)
            }

            Button {
                objectName: "resetBook"
                Layout.fillWidth: true
                visible: panel.look.bookOwn === true
                flat: true
                text: qsTr("Reset This Book")
                onClicked: panel.app.resetLook("book")
            }
            Button {
                objectName: "resetLanguage"
                Layout.fillWidth: true
                visible: panel.look.languageOwn === true
                flat: true
                text: qsTr("Reset %1 Books").arg(panel.look.languageName || "")
                onClicked: panel.app.resetLook("language")
            }

            Label { Layout.topMargin: 14; text: qsTr("PAGE TURNS (ALL BOOKS)"); font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1; color: panel.theme.muted }
            Caption { text: qsTr("Animation") }
            ComboBox {
                id: turnBox
                objectName: "turnStyle"
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "key"
                model: panel.app ? panel.app.pageTurnStyles() : []
                currentIndex: { const k = panel.look.pageTurn || "none"; for (let i = 0; i < count; ++i) if (model[i].key === k) return i; return 0 }
                onActivated: (i) => panel.app.setPageTurn(model[i].key, panel.look.pageTurnMs, panel.look.pageTurnMotion)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: panel.theme.muted
                text: turnBox.currentIndex >= 0 && turnBox.model.length > turnBox.currentIndex ? turnBox.model[turnBox.currentIndex].about : ""
            }
            Caption {
                visible: panel.look.pageTurn !== "none"
                text: qsTr("Duration: %1 ms").arg(duration.pressed ? Math.round(duration.value) : (panel.look.pageTurnMs || 0))
            }
            Slider {
                id: duration
                objectName: "turnDuration"
                visible: panel.look.pageTurn !== "none"
                Layout.fillWidth: true
                from: 100; to: 1000; stepSize: 50
                value: panel.look.pageTurnMs || 300
                onPressedChanged: if (!pressed) panel.app.setPageTurn(panel.look.pageTurn, Math.round(value), panel.look.pageTurnMotion)
            }
            Caption { visible: panel.look.pageTurn !== "none"; text: qsTr("Motion") }
            ComboBox {
                objectName: "turnMotion"
                visible: panel.look.pageTurn !== "none"
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "key"
                model: panel.app ? panel.app.pageTurnMotions() : []
                currentIndex: { const k = panel.look.pageTurnMotion || "smooth"; for (let i = 0; i < count; ++i) if (model[i].key === k) return i; return 0 }
                onActivated: (i) => panel.app.setPageTurn(panel.look.pageTurn, panel.look.pageTurnMs, model[i].key)
            }

            Label { Layout.topMargin: 14; text: qsTr("THEME"); font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1; color: panel.theme.muted }
            ThemeList {
                objectName: "themeList"
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                interactive: false
                app: panel.app
                theme: panel.theme
            }
            Button {
                text: qsTr("Settings…")
                flat: true
                onClicked: { panel.close(); panel.settingsRequested() }
            }
        }
    }
}

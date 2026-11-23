// The window: the library first; a book opens the reader on top; Settings from either. Android's Back button
// goes back a screen (and closes the book when leaving the reader).
import QtQuick
import QtQuick.Controls
import FreeReaderMobile

ApplicationWindow {
    id: win
    required property MobileApp app
    width: 412
    height: 915
    visible: true
    title: "FreeReader"
    color: appTheme.bg

    Theme { id: appTheme; colors: win.app.colors }

    // The controls (buttons, fields, sliders) in the theme's colors: a highlighted button in the accent color.
    palette.window: appTheme.bg
    palette.windowText: appTheme.fg
    palette.base: appTheme.pop
    palette.alternateBase: appTheme.surface
    palette.text: appTheme.fg
    palette.button: appTheme.surface
    palette.buttonText: appTheme.fg
    palette.dark: appTheme.accent
    palette.brightText: appTheme.accentText
    palette.mid: appTheme.line
    palette.midlight: appTheme.surface
    palette.light: appTheme.pop
    palette.highlight: appTheme.accent
    palette.highlightedText: appTheme.accentText
    palette.placeholderText: appTheme.muted
    palette.toolTipBase: appTheme.pop
    palette.toolTipText: appTheme.fg

    // The screenshot mode opens a screen by name.
    function showScreen(name) {
        if (name === "settings") stack.push(settingsScreen)
        else if (name === "words" || name === "practice") { stack.push(wordsScreen); if (name === "practice") stack.currentItem.startPractice() }
        else if (name === "menu" && stack.currentItem && stack.currentItem.showChrome) stack.currentItem.showChrome(true)
        else if (name.startsWith("part:") && stack.currentItem && stack.currentItem.showPart) stack.currentItem.showPart(name.substring(5))
    }

    StackView {
        id: stack
        anchors.fill: parent
        anchors.topMargin: win.app.safeTop
        anchors.bottomMargin: win.app.safeBottom
        anchors.leftMargin: win.app.safeLeft
        anchors.rightMargin: win.app.safeRight
        initialItem: LibraryPage {
            app: win.app
            theme: appTheme
            onSettingsRequested: stack.push(settingsScreen)
            onWordsRequested: stack.push(wordsScreen)
        }
    }

    Component {
        id: readerScreen
        ReaderPage {
            app: win.app
            theme: appTheme
            onSettingsRequested: stack.push(settingsScreen)
            onCloseRequested: stack.pop()
        }
    }
    Component {
        id: wordsScreen
        WordsPage {
            app: win.app
            theme: appTheme
            onCloseRequested: stack.pop()
        }
    }
    Component {
        id: settingsScreen
        SettingsPage {
            app: win.app
            theme: appTheme
            onCloseRequested: stack.pop()
        }
    }

    Connections {
        target: win.app
        function onBookOpened() { stack.push(readerScreen) }
    }

    onClosing: (close) => {
        if (stack.depth > 1) {
            close.accepted = false
            stack.pop()
        }
    }
}

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

    // The screenshot mode opens a screen by name.
    function showScreen(name) {
        if (name === "settings") stack.push(settingsScreen)
        else if (name === "menu" && stack.currentItem && stack.currentItem.showChrome) stack.currentItem.showChrome(true)
        else if (name.startsWith("part:") && stack.currentItem && stack.currentItem.showPart) stack.currentItem.showPart(name.substring(5))
    }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: LibraryPage {
            app: win.app
            theme: appTheme
            onSettingsRequested: stack.push(settingsScreen)
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

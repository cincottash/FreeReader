// A line icon drawn from an SVG path on a 24×24 grid (the desktop app's icon shapes), in any color and size.
import QtQuick
import QtQuick.Shapes

Item {
    id: icon
    property string name
    property color color: "black"
    property bool filled: false
    implicitWidth: 24
    implicitHeight: 24

    readonly property var paths: ({
        "back": "M15 5l-7 7 7 7",
        "search": "M17.5 11a6.5 6.5 0 1 1-13 0a6.5 6.5 0 1 1 13 0M16 16l4.5 4.5",
        "toc": "M8 6h12M8 12h12M8 18h12M4 6h.01M4 12h.01M4 18h.01",
        "bookmark": "M7 4h10v16l-5-4-5 4z",
        "notebook": "M6 4.5h12a2.5 2.5 0 0 1 2.5 2.5v10a2.5 2.5 0 0 1-2.5 2.5H6a2.5 2.5 0 0 1-2.5-2.5V7A2.5 2.5 0 0 1 6 4.5zM14.5 4.5v15",
        "close": "M6 6l12 12M18 6L6 18",
        "settings": "M12 9a3 3 0 1 1 0 6a3 3 0 1 1 0-6zM12 3v2.5M12 18.5V21M3 12h2.5M18.5 12H21M5.6 5.6l1.8 1.8M16.6 16.6l1.8 1.8M5.6 18.4l1.8-1.8M16.6 7.4l1.8-1.8",
        "note": "M5 19.5l1-4.5L16 5l3.5 3.5-10 10zM14 7l3.5 3.5",
        "copy": "M9 9h10v11H9zM5 15V4h10",
        "dictionary": "M5 4.5h9.5a2 2 0 0 1 2 2V20H7a2 2 0 0 1-2-2zM5 18a2 2 0 0 1 2-2h9.5M19 7v13",
        "translate": "M4 6h9M8.5 4v2M6 6c.5 3 2.5 5.5 5 7M11 6c-.5 3-2.5 5.5-5.5 7.5M13 20l3.5-8 3.5 8M14.2 17.3h4.6",
        "trash": "M5 7h14M10 7V5h4v2M7 7l1 13h8l1-13",
        "plus": "M12 5v14M5 12h14",
        "theme": "M12 4.5a7.5 7.5 0 1 1 0 15a7.5 7.5 0 1 1 0-15z",
        "explain": "M4 5h16v11H10l-5 4v-4H4zM8 9h8M8 12h5",
        "speaker": "M4 9.5h4l5-4v13l-5-4H4zM16.5 9a4.5 4.5 0 0 1 0 6M19 6.5a8.5 8.5 0 0 1 0 11",
        "stop": "M7 7h10v10H7z",
        "flip": "M4 9h13l-3-3M20 15H7l3 3",
        "chapterPrevious": "M7 6v12M17 6l-6 6 6 6",
        "chapterNext": "M17 6v12M7 6l6 6-6 6",
        "beginning": "M5 6v12M12 6l-6 6 6 6M19 6l-6 6 6 6",
        "undo": "M9 7L5 11l4 4M5 11h9a5 5 0 0 1 0 10h-3",
        "grid": "M5 5h6v6H5zM13 5h6v6h-6zM5 13h6v6H5zM13 13h6v6h-6z",
        "list": "M4 6h16M4 12h16M4 18h16",
        "share": "M12 4v11M8 8l4-4 4 4M6 12v7h12v-7"
    })

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: Math.min(icon.width, icon.height) / 24
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: icon.color
            strokeWidth: 1.8
            fillColor: icon.filled ? icon.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
}

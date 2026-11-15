import QtQuick
import QtQuick.Shapes
import QtQuick.Window

// One family of line icons, drawn the way Omarchy's Files draws its own: a
// 24-unit grid, one stroke, ends and corners that follow the theme's rounding.
Item {
    id: glyph

    property string name: "close"
    property color color: theme.foreground
    // One grid unit is a whole (or half) device pixel where the screen's
    // scale allows it, and the drawing starts on a device pixel, so straight
    // strokes stay sharp at fractional scales such as 1.25.
    readonly property real dpr: Screen.devicePixelRatio
    readonly property real fitted: Math.max(0.5, Math.round(18 * dpr / 24 * 2) / 2) * 24 / dpr
    property real size: Math.abs(fitted - 18) <= 2.5 ? fitted : 18
    property real stroke: Math.max(1, Math.round(1.6 * dpr)) / dpr

    function snap() {
        const at = glyph.mapToItem(null, 0, 0)
        shape.x = Math.round(at.x * dpr) / dpr - at.x
        shape.y = Math.round(at.y * dpr) / dpr - at.y
    }
    Component.onCompleted: Qt.callLater(snap)
    onVisibleChanged: Qt.callLater(snap)
    onXChanged: Qt.callLater(snap)
    onYChanged: Qt.callLater(snap)
    Connections {
        target: glyph.Window.window
        function onWidthChanged() { Qt.callLater(glyph.snap) }
        function onHeightChanged() { Qt.callLater(glyph.snap) }
    }

    implicitWidth: size
    implicitHeight: size

    function circle(cx, cy, r) {
        return "M" + (cx - r) + " " + cy + "a" + r + " " + r + " 0 1 0 " + (2 * r) + " 0a" + r + " " + r + " 0 1 0 " + (-2 * r) + " 0"
    }

    readonly property string path: {
        switch (name) {
        case "back": return "M15 6l-6 6 6 6"
        case "forward": return "M9 6l6 6-6 6"
        case "edit": return "M4 7h9M19 7h1M4 17h1M11 17h9" + circle(16, 7, 3) + circle(8, 17, 3)
        case "looks": return circle(12, 9, 5) + circle(8.5, 15, 5) + circle(15.5, 15, 5)
        case "compress": return "M4 4l6 6M10 5v5H5M20 20l-6-6M14 19v-5h5"
        case "details": return circle(12, 12, 9) + "M12 11v6M12 7v.5"
        case "rotate-left": return "M4 5v5h5M5.5 15a7 7 0 1 0 1-7.5L4 10"
        case "rotate-right": return "M20 5v5h-5M18.5 15a7 7 0 1 1-1-7.5L20 10"
        case "flip": return "M12 3v18M8 8l-4 4 4 4M16 8l4 4-4 4"
        case "flip-up": return "M3 12h18M8 8l4-4 4 4M8 16l4 4 4-4"
        case "undo": return "M8 6l-4 4 4 4M4 10h10a5 5 0 0 1 0 10h-3"
        case "redo": return "M16 6l4 4-4 4M20 10H10a5 5 0 0 0 0 10h3"
        case "add": return "M12 5v14M5 12h14"
        case "compare": return "M9 8l-4 4 4 4M15 8l4 4-4 4"
        case "folder": return theme.radius > 0 ? "M3 8Q3 6 5 6H10L12 8H19Q21 8 21 10V17Q21 19 19 19H5Q3 19 3 17Z" : "M3 6h7l2 2h9v11H3z"
        case "open": return "M14 4h6v6M20 4l-9 9M18 14v6H4V6h6"
        case "trash": return "M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"
        case "check": return "M5 12l5 5 9-10"
        // A frame with a hill and the sun, for where a photo will be.
        case "picture": return "M3 5h18v14H3zM3 16l5-5 4 4 3-3 6 6" + circle(16, 9, 1.5)
        // The command loop, the usual mark for keyboard shortcuts: a square
        // with a ring at each corner, drawn in one stroke like the others.
        case "keyboard": return "M9 9h6v6H9z"
                                + "M9 9V7a2 2 0 1 0-2 2zM15 9V7a2 2 0 1 1 2 2zM15 15v2a2 2 0 1 0 2-2zM9 15v2a2 2 0 1 1-2-2z"
        default: return "M6 6l12 12M18 6L6 18"
        }
    }

    Shape {
        id: shape
        width: glyph.size
        height: glyph.size
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: glyph.color
            fillColor: "transparent"
            strokeWidth: glyph.stroke
            capStyle: theme.radius > 0 ? ShapePath.RoundCap : ShapePath.SquareCap
            joinStyle: theme.radius > 0 ? ShapePath.RoundJoin : ShapePath.MiterJoin
            scale: Qt.size(glyph.size / 24, glyph.size / 24)
            PathSvg { path: glyph.path }
        }
    }
}

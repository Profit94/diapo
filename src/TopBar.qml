import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// The bar over every view, as in Files' preview: what this is on the left,
// what can be done with it on the right.
Rectangle {
    id: bar
    property string title
    property string subtitle
    default property alias actions: row.data

    height: 60
    color: theme.background

    // Dragging the bar (not its buttons) moves the window, as a title bar
    // does, so Diapo can be slid aside while it floats over Files.
    Item {
        objectName: "barDrag"
        anchors.left: parent.left
        anchors.right: row.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        DragHandler {
            target: null
            onActiveChanged: if (active && bar.Window.window && bar.Window.window.visibility !== Window.FullScreen)
                bar.Window.window.startSystemMove()
        }
    }
    Column {
        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.right: row.left
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3
        Text {
            width: parent.width
            text: bar.title
            color: theme.foreground
            font.pixelSize: 14
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: bar.subtitle
            color: Qt.alpha(theme.foreground, 0.62)
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
    RowLayout {
        id: row
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1 / Screen.devicePixelRatio
        color: Qt.alpha(theme.foreground, 0.08)
    }
}

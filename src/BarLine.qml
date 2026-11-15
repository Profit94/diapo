import QtQuick
import QtQuick.Window

// The hairline between two groups of toolbar buttons.
Item {
    implicitWidth: 17
    implicitHeight: 20
    Rectangle {
        anchors.centerIn: parent
        width: 1 / Screen.devicePixelRatio
        height: 20
        color: Qt.alpha(theme.foreground, 0.14)
    }
}

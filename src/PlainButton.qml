import QtQuick
import QtQuick.Controls

// A button in Files' look: the accent when it is the main choice, red when it
// throws something away, quiet otherwise. No ripple, no animation.
AbstractButton {
    id: button
    property bool primary: false
    property bool destructive: false
    readonly property bool filled: primary || destructive
    readonly property color fill: destructive ? theme.colors.red : theme.accent

    // Material gives buttons its own larger face; these follow the app's.
    font.family: Qt.application.font.family
    font.pixelSize: 12
    font.weight: Font.Normal
    font.capitalization: Font.MixedCase
    font.letterSpacing: 0
    implicitWidth: Math.max(88, label.implicitWidth + 32)
    implicitHeight: 36
    hoverEnabled: true
    Accessible.name: text
    focusPolicy: Qt.TabFocus
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        opacity: button.enabled ? 1 : 0.45
        color: button.filled ? (button.hovered ? Qt.lighter(button.fill, 1.08) : button.fill)
             : button.hovered ? Qt.alpha(theme.foreground, 0.10)
             : Qt.alpha(theme.foreground, 0.05)

        Rectangle {
            visible: button.activeFocus
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 2
            border.color: button.fill
        }
    }
    contentItem: Text {
        id: label
        text: button.text
        font: button.font
        opacity: button.enabled ? 1 : 0.6
        color: button.destructive ? theme.background : button.primary ? theme.accentForeground : theme.foreground
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}

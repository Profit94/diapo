import QtQuick
import QtQuick.Controls

// A few choices side by side in one track, one of them picked.
Rectangle {
    id: track
    // [{ value, label }]
    property var options: []
    property var value
    signal picked(var value)

    implicitHeight: 38
    radius: Math.min(6, theme.radius)
    color: Qt.alpha(theme.foreground, 0.05)

    Row {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 4
        Repeater {
            model: track.options
            delegate: AbstractButton {
                id: option
                required property var modelData
                width: (parent.width - 4 * (track.options.length - 1)) / track.options.length
                height: parent.height
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                // Material gives buttons its own larger face; these follow the app's.
                font.family: Qt.application.font.family
                font.pixelSize: 12
                font.weight: Font.Normal
                font.capitalization: Font.MixedCase
                font.letterSpacing: 0
                text: modelData.label
                Accessible.name: text
                onClicked: track.picked(modelData.value)
                background: Rectangle {
                    radius: Math.min(4, theme.radius)
                    color: track.value === option.modelData.value ? Qt.alpha(theme.accent, 0.18)
                         : option.hovered ? Qt.alpha(theme.foreground, 0.06) : "transparent"
                }
                contentItem: Text {
                    text: option.text
                    font: option.font
                    color: Qt.alpha(theme.foreground, track.value === option.modelData.value ? 1 : 0.65)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }
}

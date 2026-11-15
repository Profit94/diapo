import QtQuick
import QtQuick.Controls

// A square to tick, with what it means beside it.
AbstractButton {
    id: row
    checkable: true
    // Material gives buttons its own larger face; these follow the app's.
    font.family: Qt.application.font.family
    font.pixelSize: 12
    font.weight: Font.Normal
    font.capitalization: Font.MixedCase
    font.letterSpacing: 0
    implicitHeight: 30
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: text
    background: null
    contentItem: Row {
        spacing: 10
        Rectangle {
            width: 16
            height: 16
            anchors.verticalCenter: parent.verticalCenter
            radius: Math.min(3, theme.radius)
            color: row.checked ? theme.accent : "transparent"
            border.width: row.checked ? 0 : 1
            border.color: Qt.alpha(theme.foreground, row.activeFocus || row.hovered ? 0.7 : 0.4)
            Glyph {
                anchors.centerIn: parent
                visible: row.checked
                name: "check"
                size: 12
                stroke: 2.4
                color: theme.accentForeground
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: row.text
            font: row.font
            color: theme.foreground
        }
    }
}

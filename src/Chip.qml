import QtQuick
import QtQuick.Controls

// One choice among a few: quiet until picked, then tinted with the accent.
AbstractButton {
    id: chip
    property bool picked: false
    property string note

    // Material gives buttons its own larger face; these follow the app's.
    font.family: Qt.application.font.family
    font.pixelSize: 12
    font.weight: Font.Normal
    font.capitalization: Font.MixedCase
    font.letterSpacing: 0
    implicitWidth: row.implicitWidth + 20
    implicitHeight: 30
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: picked

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        opacity: chip.enabled ? 1 : 0.4
        color: chip.picked ? Qt.alpha(theme.accent, 0.18)
             : chip.hovered ? Qt.alpha(theme.foreground, 0.10)
             : Qt.alpha(theme.foreground, 0.05)
    }
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 5
            Text {
                text: chip.text
                font: chip.font
                width: Math.min(implicitWidth, chip.width - 12 - (chip.note.length > 0 ? 22 : 0))
                elide: Text.ElideRight
                color: Qt.alpha(theme.foreground, chip.enabled ? 1 : 0.5)
            }
            Text {
                visible: chip.note.length > 0
                text: chip.note
                font.pixelSize: 10
                color: Qt.alpha(theme.foreground, 0.6)
                anchors.baseline: parent.children[0].baseline
            }
        }
    }
}

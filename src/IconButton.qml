import QtQuick
import QtQuick.Controls

// A square toolbar button holding one glyph, as in Files. `text` is its
// accessible name and tooltip, never drawn.
AbstractButton {
    id: button

    property string glyph
    property bool active: false
    // A tool in a panel sits on a faint tile, like the choices beside it.
    property bool tile: false

    implicitWidth: 36
    implicitHeight: 36
    focusPolicy: Qt.NoFocus
    hoverEnabled: true

    Accessible.name: text
    ToolTip {
        visible: button.hovered && button.text.length > 0
        delay: 600
        text: button.text
        enter: null
        exit: null
    }

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: button.active ? Qt.alpha(theme.accent, 0.16)
             : button.hovered && button.enabled ? Qt.alpha(theme.foreground, 0.10)
             : button.tile ? Qt.alpha(theme.foreground, 0.05) : "transparent"
    }

    contentItem: Item {
        Glyph {
            anchors.centerIn: parent
            name: button.glyph
            color: Qt.alpha(theme.foreground, button.enabled ? 1 : 0.35)
        }
    }
}

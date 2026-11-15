import QtQuick

// A name, a flat slider, and its value. A double click puts it back.
Item {
    id: row
    property string label
    property real from: -100
    property real to: 100
    property real value: 0
    property real step: 1
    property real resting: 0
    property string display: String(Math.round(value))
    property int labelWidth: 92
    signal moved(real value)
    signal released()

    implicitHeight: 30
    opacity: enabled ? 1 : 0.4
    activeFocusOnTab: true
    Accessible.role: Accessible.Slider
    Accessible.name: label

    function nudge(by) {
        const next = Math.max(from, Math.min(to, value + by * step))
        if (next !== value) {
            moved(next)
            released()
        }
    }
    Keys.onLeftPressed: nudge(-1)
    Keys.onRightPressed: nudge(1)

    Text {
        id: name
        width: row.labelWidth
        anchors.verticalCenter: parent.verticalCenter
        text: row.label
        color: theme.foreground
        font.pixelSize: 12
        elide: Text.ElideRight
    }
    Text {
        id: number
        width: 44
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignRight
        text: row.display
        font.pixelSize: 11
        color: Qt.alpha(theme.foreground, 0.62)
    }
    Item {
        id: track
        anchors.left: name.right
        anchors.right: number.left
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        height: parent.height

        readonly property real position: (row.value - row.from) / (row.to - row.from)
        readonly property real rest: (row.resting - row.from) / (row.to - row.from)

        Rectangle {
            width: parent.width
            height: 2
            anchors.verticalCenter: parent.verticalCenter
            color: Qt.alpha(theme.foreground, 0.16)
        }
        // From where the slider rests to where it is now.
        Rectangle {
            x: Math.min(track.position, track.rest) * track.width
            width: Math.abs(track.position - track.rest) * track.width
            height: 2
            anchors.verticalCenter: parent.verticalCenter
            color: theme.accent
        }
        Rectangle {
            x: track.position * (track.width - width)
            width: 10
            height: 16
            anchors.verticalCenter: parent.verticalCenter
            radius: Math.min(3, theme.radius)
            color: row.activeFocus || area.pressed ? theme.accent : theme.foreground
        }
        MouseArea {
            id: area
            anchors.fill: parent
            anchors.margins: -4
            preventStealing: true
            function pick(x) {
                const t = Math.max(0, Math.min(1, (x - 4 - 5) / (track.width - 10)))
                const raw = row.from + t * (row.to - row.from)
                const next = Math.round(raw / row.step) * row.step
                if (next !== row.value)
                    row.moved(next)
            }
            onPressed: (mouse) => pick(mouse.x)
            onPositionChanged: (mouse) => pick(mouse.x)
            onReleased: row.released()
            onDoubleClicked: {
                row.moved(row.resting)
                row.released()
            }
        }
    }
}

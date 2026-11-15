import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every shortcut, grouped by what it is for, with the group for the screen
// in use first. Opened with ? or the keyboard button in the bar.
Popup {
    id: popup
    // "view", "edit" or "compress"
    property string mode: "view"

    readonly property var groups: [
        { id: "view", title: qsTr("Viewing"), keys: [
            { keys: ["Space", "→"], action: qsTr("Next photo") },
            { keys: ["Backspace", "←"], action: qsTr("Previous photo") },
            { keys: ["Home", "End"], action: qsTr("First and last photo") },
            { keys: ["+", "−"], action: qsTr("Zoom in and out") },
            { keys: ["0", "1"], action: qsTr("Fit, and actual size") },
            { keys: ["Z"], action: qsTr("Fill the window, or fit") },
            { keys: ["F"], action: qsTr("Full screen") },
            { keys: ["I"], action: qsTr("Details") },
            { keys: ["Ctrl", "C"], action: qsTr("Copy the photo") },
            { keys: ["Delete"], action: qsTr("Move to the trash") },
            { keys: ["Ctrl", "O"], action: qsTr("Open a photo") }
        ] },
        { id: "edit", title: qsTr("Editing"), keys: [
            { keys: ["E"], action: qsTr("Edit") },
            { keys: ["L"], action: qsTr("Looks") },
            { keys: ["["], action: qsTr("Rotate left") },
            { keys: ["]"], action: qsTr("Rotate right") },
            { keys: ["\\"], action: qsTr("Hold to see the original") },
            { keys: ["Ctrl", "Z"], action: qsTr("Undo") },
            { keys: ["Ctrl", "Shift", "Z"], action: qsTr("Redo") },
            { keys: ["Ctrl", "S"], action: qsTr("Save a copy") }
        ] },
        { id: "compress", title: qsTr("Compressing"), keys: [
            { keys: ["C"], action: qsTr("Compress") },
            { keys: ["Ctrl", "O"], action: qsTr("Add photos") },
            { keys: ["Enter"], action: qsTr("Compress, then save") }
        ] },
        { id: "app", title: qsTr("Anywhere"), keys: [
            { keys: ["?"], action: qsTr("Show the shortcuts") },
            { keys: ["Esc"], action: qsTr("Go back") },
            { keys: ["Q"], action: qsTr("Quit") }
        ] }
    ]
    // The screen in use first, then the others in their usual order.
    readonly property var ordered: groups.filter((g) => g.id === mode).concat(groups.filter((g) => g.id !== mode))

    anchors.centerIn: Overlay.overlay
    width: Math.min(640, (Overlay.overlay ? Overlay.overlay.width : 640) - 32)
    height: Math.min(content.implicitHeight + topPadding + bottomPadding, (Overlay.overlay ? Overlay.overlay.height : 600) - 32)
    modal: true
    focus: true
    padding: 0
    enter: null
    exit: null
    background: Rectangle {
        color: theme.background
        border.color: Qt.alpha(theme.foreground, 0.16)
        radius: theme.radius
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        spacing: 0
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 12
            Layout.preferredHeight: 56
            Text {
                Layout.fillWidth: true
                text: qsTr("Keyboard shortcuts")
                color: theme.foreground
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            IconButton { glyph: "close"; text: qsTr("Close"); onClicked: popup.close() }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Qt.alpha(theme.foreground, 0.08) }

        Flickable {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: grid.implicitHeight + 40
            clip: true
            contentHeight: grid.implicitHeight + 40
            boundsBehavior: Flickable.StopAtBounds

            // Two columns when there is room, one when the window is narrow.
            GridLayout {
                id: grid
                x: 24
                y: 20
                width: scroll.width - 48
                columns: width > 480 ? 2 : 1
                columnSpacing: 32
                rowSpacing: 24

                Repeater {
                    model: popup.ordered
                    ColumnLayout {
                        id: group
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 1
                        spacing: 8
                        Heading { text: group.modelData.title; bottomPadding: 2 }
                        Repeater {
                            model: group.modelData.keys
                            RowLayout {
                                id: line
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 12
                                Text {
                                    Layout.fillWidth: true
                                    text: line.modelData.action
                                    color: theme.foreground
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Row {
                                    spacing: 4
                                    Repeater {
                                        model: line.modelData.keys
                                        // One key, drawn as a key cap.
                                        Rectangle {
                                            required property string modelData
                                            width: Math.max(22, cap.implicitWidth + 12)
                                            height: 22
                                            radius: Math.min(4, theme.radius)
                                            color: Qt.alpha(theme.foreground, 0.06)
                                            border.width: 1
                                            border.color: Qt.alpha(theme.foreground, 0.14)
                                            Text {
                                                id: cap
                                                anchors.centerIn: parent
                                                text: parent.modelData
                                                color: theme.foreground
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

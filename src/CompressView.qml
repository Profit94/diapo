import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// The compressor: the original against the result under one line to drag,
// and the few choices that decide the new size.
Item {
    id: view
    property real split: 0.5
    signal leave()
    signal askSave()
    signal showShortcuts()

    readonly property bool known: compressor.estimateBytes >= 0 && !compressor.estimating
    readonly property int saving: compressor.originalBytes > 0 && compressor.estimateBytes >= 0
        ? Math.round(100 * (1 - compressor.estimateBytes / compressor.originalBytes)) : 0

    TopBar {
        id: bar
        width: parent.width
        title: qsTr("Compress")
        subtitle: compressor.count > 1 ? qsTr("%Ln photos", "", compressor.count)
                : compressor.count === 1 ? compressor.files[0].name : qsTr("Drop photos here")

        IconButton { glyph: "add"; text: qsTr("Add photos"); onClicked: compressor.addPhotos() }
        IconButton { glyph: "keyboard"; text: qsTr("Keyboard shortcuts"); onClicked: view.showShortcuts() }
        IconButton { glyph: "close"; text: qsTr("Close"); onClicked: view.leave() }
    }

    Rectangle {
        id: stage
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: panel.left
        color: win.well
        clip: true

        Item {
            id: pictures
            anchors.fill: parent
            anchors.bottomMargin: strip.visible ? strip.height : 0

            readonly property string made: compressor.revision > 0 ? "image://made/mem/" + compressor.revision + "/" : ""
            readonly property bool both: compressor.estimateBytes >= 0

            Image {
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                cache: false
                source: pictures.made && pictures.both ? pictures.made + "cmp-b" : ""
            }
            Item {
                width: pictures.both ? Math.round(pictures.width * view.split) : pictures.width
                height: parent.height
                clip: true
                Image {
                    id: original
                    width: pictures.width
                    height: pictures.height
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    source: pictures.made ? pictures.made + "cmp-a" : ""
                }
            }
            Rectangle {
                id: line
                visible: pictures.both
                x: Math.round(pictures.width * view.split) - 1
                width: 2
                height: parent.height
                color: "#f2f2f2"
                Rectangle {
                    anchors.centerIn: parent
                    width: 36
                    height: 36
                    radius: Math.min(4, theme.radius)
                    color: "#f2f2f2"
                    Glyph { anchors.centerIn: parent; name: "compare"; color: "#1a1a1a" }
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: pictures.both
                preventStealing: true
                cursorShape: Qt.SizeHorCursor
                function drag(x) { view.split = Math.max(0.02, Math.min(0.98, x / width)) }
                onPressed: (mouse) => drag(mouse.x)
                onPositionChanged: (mouse) => drag(mouse.x)
            }
            Repeater {
                model: [{ right: false, text: qsTr("Original · %1").arg(backend.formatBytes(compressor.originalBytes)) },
                        { right: true, text: view.known ? qsTr("Result · %1").arg(backend.formatBytes(compressor.estimateBytes))
                                                        : qsTr("Result · working…") }]
                Rectangle {
                    required property var modelData
                    visible: compressor.count > 0 && original.status === Image.Ready
                    x: modelData.right ? parent.width - width - 12 : 12
                    y: 12
                    width: tag.implicitWidth + 16
                    height: 26
                    radius: Math.min(4, theme.radius)
                    color: Qt.rgba(0, 0, 0, 0.6)
                    Text { id: tag; anchors.centerIn: parent; text: modelData.text; color: "#f2f2f2"; font.pixelSize: 11 }
                }
            }
            Column {
                anchors.centerIn: parent
                visible: compressor.count === 0
                spacing: 12
                Glyph { anchors.horizontalCenter: parent.horizontalCenter; name: "picture"; size: 36; stroke: 1.2; color: Qt.alpha(theme.foreground, 0.4) }
                Text { text: qsTr("Drop photos here, or press Ctrl+O"); color: Qt.alpha(theme.foreground, 0.62) }
            }
        }

        // With several photos, the one on show is picked here.
        ListView {
            id: strip
            visible: compressor.count > 1
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            height: 46
            orientation: ListView.Horizontal
            spacing: 4
            clip: true
            model: compressor.files
            delegate: Chip {
                required property var modelData
                required property int index
                y: 8
                text: modelData.name
                note: modelData.result >= 0 ? backend.formatBytes(modelData.result) : backend.formatBytes(modelData.bytes)
                picked: compressor.selected === index
                onClicked: compressor.select(index)
                onPressAndHold: compressor.remove(index)
            }
        }
    }

    Rectangle {
        id: panel
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: Math.min(340, parent.width * 0.45)
        color: theme.background

        Rectangle { width: 1 / Screen.devicePixelRatio; height: parent.height; color: Qt.alpha(theme.foreground, 0.08) }

        Flickable {
            id: scroll
            anchors.fill: parent
            anchors.topMargin: 16
            anchors.bottomMargin: 72
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            clip: true
            contentHeight: options.height
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: options
                width: scroll.width
                spacing: 22

                Column {
                    width: parent.width
                    spacing: 4
                    RowLayout {
                        width: parent.width
                        spacing: 10
                        Heading { Layout.fillWidth: true; text: qsTr("New size") }
                        Text {
                            visible: view.known
                            font.pixelSize: 11
                            color: view.saving >= 0 ? theme.colors.green : theme.colors.red
                            text: view.saving >= 0 ? qsTr("%1% smaller").arg(view.saving) : qsTr("%1% larger").arg(-view.saving)
                        }
                        Text {
                            objectName: "newSize"
                            font.pixelSize: 13
                            color: theme.foreground
                            font.weight: Font.DemiBold
                            text: view.known ? backend.formatBytes(compressor.estimateBytes) : compressor.count > 0 ? "…" : "—"
                        }
                    }
                    Text {
                        width: parent.width
                        visible: view.known
                        font.pixelSize: 11
                        color: Qt.alpha(theme.foreground, 0.62)
                        elide: Text.ElideRight
                        text: qsTr("was %1").arg(backend.formatBytes(compressor.originalBytes)) + " · "
                              + compressor.originalSize.width + " × " + compressor.originalSize.height + " → "
                              + compressor.estimateSize.width + " × " + compressor.estimateSize.height
                    }
                }

                Column {
                    width: parent.width
                    spacing: 10
                    Heading { text: qsTr("Preset") }
                    Segmented {
                        width: parent.width
                        options: [{ value: 0, label: qsTr("Small") }, { value: 1, label: qsTr("Balanced") }, { value: 2, label: qsTr("Best") }]
                        value: compressor.preset
                        onPicked: (value) => compressor.setPreset(value)
                    }
                    Text {
                        width: parent.width
                        font.pixelSize: 11
                        color: Qt.alpha(theme.foreground, 0.62)
                        wrapMode: Text.Wrap
                        text: compressor.preset === 0 ? qsTr("For sending and uploading.")
                            : compressor.preset === 1 ? qsTr("Hard to tell from the original.")
                            : compressor.preset === 2 ? qsTr("Full size, for keeping.") : qsTr("Your own settings.")
                    }
                }

                Column {
                    width: parent.width
                    spacing: 10
                    Heading { text: qsTr("Format") }
                    ChipGrid {
                        width: parent.width
                        columns: 3
                        options: [{ value: "webp", label: "WebP" }, { value: "avif", label: "AVIF" }, { value: "jxl", label: "JPEG XL" },
                                  { value: "jpeg", label: "JPEG" }, { value: "png", label: "PNG" }, { value: "same", label: qsTr("Same") }]
                        value: compressor.format
                        onPicked: (value) => compressor.setFormat(value)
                    }
                    Text {
                        width: parent.width
                        font.pixelSize: 11
                        color: Qt.alpha(theme.foreground, 0.62)
                        wrapMode: Text.Wrap
                        text: compressor.format === "webp" ? qsTr("Small files that open almost everywhere.")
                            : compressor.format === "avif" ? qsTr("The smallest files. Slower to save, and older apps may not open it.")
                            : compressor.format === "jxl" ? qsTr("Very small at high quality. Few apps open it yet.")
                            : compressor.format === "jpeg" ? qsTr("Opens everywhere. Larger than WebP at the same quality.")
                            : compressor.format === "png" ? qsTr("No quality loss, but files stay large.")
                            : qsTr("The format each photo already has.")
                    }
                }

                SliderRow {
                    width: parent.width
                    labelWidth: 76
                    enabled: compressor.format !== "png"
                    label: qsTr("Quality")
                    from: 1; to: 100; resting: 1
                    value: compressor.quality
                    onMoved: (value) => compressor.setQuality(value)
                }

                Column {
                    width: parent.width
                    spacing: 10
                    Heading { text: qsTr("Longest side") }
                    ChipGrid {
                        width: parent.width
                        columns: 3
                        options: [{ value: 0, label: qsTr("Original") }, { value: 3840, label: "3840" }, { value: 2560, label: "2560" },
                                  { value: 1920, label: "1920" }, { value: 1280, label: "1280" }, { value: 1080, label: "1080" }]
                        value: compressor.maxEdge
                        onPicked: (value) => compressor.setMaxEdge(value)
                    }
                }

                CheckRow {
                    text: qsTr("Remove location and camera data")
                    checked: compressor.strip
                    onClicked: compressor.setStrip(!compressor.strip)
                }
            }
        }

        Column {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 16
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 8
            Rectangle {
                width: parent.width
                height: 2
                visible: compressor.running
                color: Qt.alpha(theme.foreground, 0.16)
                Rectangle { width: parent.width * compressor.progress; height: 2; color: theme.accent }
            }
            PlainButton {
                objectName: "compressGo"
                width: parent.width
                implicitHeight: 40
                primary: true
                focusPolicy: Qt.NoFocus
                enabled: compressor.count > 0 && !compressor.running
                text: compressor.running ? qsTr("Compressing…")
                    : compressor.resultReady ? qsTr("Save…")
                    : compressor.count > 1 ? qsTr("Compress %Ln photos", "", compressor.count) : qsTr("Compress")
                onClicked: compressor.resultReady ? view.askSave() : compressor.compress()
            }
        }
    }
}

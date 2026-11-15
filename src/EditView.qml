import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// The editor: the photo on the left with its crop frame, and one panel on the
// right that is either the sliders or the film looks.
FocusScope {
    id: view
    property string tab: "adjust"
    property bool comparing: false
    property string group: "Leica"
    signal leave()
    signal cancel()
    signal showShortcuts()

    // Holding the backslash shows the photo before the sliders and the look.
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Backslash && !event.isAutoRepeat) {
            comparing = true
            event.accepted = true
        }
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Backslash && !event.isAutoRepeat) {
            comparing = false
            event.accepted = true
        }
    }
    onVisibleChanged: {
        comparing = false
        if (visible) {
            forceActiveFocus()
            const current = editor.looks.find((look) => look.name === editor.look)
            if (current)
                group = current.group
        }
    }

    TopBar {
        id: bar
        width: parent.width
        title: qsTr("Edit")
        subtitle: qsTr("%1 · the original is never changed").arg(editor.name)

        IconButton { glyph: "undo"; text: qsTr("Undo"); enabled: editor.canUndo; onClicked: editor.undo() }
        IconButton { glyph: "redo"; text: qsTr("Redo"); enabled: editor.canRedo; onClicked: editor.redo() }
        IconButton { glyph: "keyboard"; text: qsTr("Keyboard shortcuts"); onClicked: view.showShortcuts() }
        BarLine {}
        PlainButton { objectName: "editCancel"; text: qsTr("Cancel"); focusPolicy: Qt.NoFocus; onClicked: view.cancel() }
        PlainButton {
            objectName: "editSave"
            Layout.leftMargin: 4
            text: editor.saving ? qsTr("Saving…") : qsTr("Save a copy")
            primary: true
            focusPolicy: Qt.NoFocus
            enabled: editor.ready && !editor.saving
            onClicked: editor.save()
        }
    }

    Rectangle {
        id: stage
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: panel.left
        color: win.well
        clip: true

        Image {
            id: preview
            anchors.fill: parent
            anchors.margins: view.tab === "adjust" ? 14 : 0
            fillMode: Image.PreserveAspectFit
            cache: false
            source: editor.ready ? "image://made/mem/" + editor.revision + "/" + (view.comparing ? "edit-plain" : "edit") : ""
        }
        Text {
            anchors.centerIn: parent
            visible: !editor.ready
            text: qsTr("Opening…")
            color: Qt.alpha(theme.foreground, 0.62)
        }
        // In the looks tab a press on the photo compares, like the backslash.
        MouseArea {
            anchors.fill: parent
            enabled: view.tab === "looks"
            onPressed: view.comparing = true
            onReleased: view.comparing = false
            onCanceled: view.comparing = false
        }

        // The crop frame, over the whole picture, dimming what will go.
        Item {
            id: frame
            objectName: "cropFrame"
            visible: view.tab === "adjust" && editor.ready && !view.comparing
            x: preview.x + (preview.width - preview.paintedWidth) / 2
            y: preview.y + (preview.height - preview.paintedHeight) / 2
            width: preview.paintedWidth
            height: preview.paintedHeight

            readonly property rect crop: editor.crop
            readonly property real cropLeft: crop.x * width
            readonly property real cropTop: crop.y * height
            readonly property real cropWidth: crop.width * width
            readonly property real cropHeight: crop.height * height
            readonly property color shade: Qt.rgba(0, 0, 0, 0.55)

            // Moves one corner (cx, cy are 0 or 1) to a point in 0 to 1,
            // keeping the chosen shape when there is one.
            function dragCorner(cx, cy, mx, my) {
                let l = crop.x, t = crop.y, r = crop.x + crop.width, b = crop.y + crop.height
                mx = Math.max(0, Math.min(1, mx))
                my = Math.max(0, Math.min(1, my))
                if (cx === 0) l = Math.min(mx, r - 0.05); else r = Math.max(mx, l + 0.05)
                if (cy === 0) t = Math.min(my, b - 0.05); else b = Math.max(my, t + 0.05)
                const ratio = editor.aspectRatio
                if (ratio > 0) {
                    const perWidth = width / (ratio * height)
                    let w = r - l, h = w * perWidth
                    const room = cy === 0 ? b : 1 - t
                    if (h > room) {
                        h = room
                        w = h / perWidth
                    }
                    if (cx === 0) l = r - w; else r = l + w
                    if (cy === 0) t = b - h; else b = t + h
                }
                editor.setCrop(l, t, r - l, b - t)
            }

            Rectangle { color: frame.shade; width: parent.width; height: frame.cropTop }
            Rectangle { color: frame.shade; width: parent.width; y: frame.cropTop + frame.cropHeight; height: parent.height - y }
            Rectangle { color: frame.shade; y: frame.cropTop; width: frame.cropLeft; height: frame.cropHeight }
            Rectangle { color: frame.shade; y: frame.cropTop; x: frame.cropLeft + frame.cropWidth; width: parent.width - x; height: frame.cropHeight }

            Rectangle {
                id: box
                objectName: "cropBox"
                x: frame.cropLeft
                y: frame.cropTop
                width: frame.cropWidth
                height: frame.cropHeight
                color: "transparent"
                border.width: 1
                border.color: "#e8e8e8"

                Repeater {
                    model: 2
                    Rectangle { required property int index; x: box.width * (index + 1) / 3; width: 1; height: box.height; color: Qt.rgba(1, 1, 1, 0.3) }
                }
                Repeater {
                    model: 2
                    Rectangle { required property int index; y: box.height * (index + 1) / 3; height: 1; width: box.width; color: Qt.rgba(1, 1, 1, 0.3) }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeAllCursor
                    preventStealing: true
                    property point start
                    property rect from
                    onPressed: (mouse) => {
                        start = mapToItem(frame, mouse.x, mouse.y)
                        from = frame.crop
                    }
                    onPositionChanged: (mouse) => {
                        const now = mapToItem(frame, mouse.x, mouse.y)
                        editor.setCrop(from.x + (now.x - start.x) / frame.width, from.y + (now.y - start.y) / frame.height,
                                       from.width, from.height)
                    }
                    onReleased: editor.commit()
                }
            }
            Repeater {
                model: [[0, 0], [1, 0], [0, 1], [1, 1]]
                Rectangle {
                    id: handle
                    required property var modelData
                    objectName: "cropHandle" + modelData[0] + modelData[1]
                    width: 12
                    height: 12
                    radius: Math.min(3, theme.radius)
                    color: "#f2f2f2"
                    // A thin dark edge keeps them visible where they stand
                    // out over a light theme's background, not the photo.
                    border.width: 1
                    border.color: Qt.rgba(0, 0, 0, 0.35)
                    x: frame.cropLeft + modelData[0] * frame.cropWidth - 6
                    y: frame.cropTop + modelData[1] * frame.cropHeight - 6
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        preventStealing: true
                        cursorShape: handle.modelData[0] === handle.modelData[1] ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                        // Where on the handle it was grabbed, so the corner
                        // keeps that distance from the pointer instead of
                        // jumping onto it.
                        property point grab
                        onPressed: (mouse) => {
                            const at = mapToItem(frame, mouse.x, mouse.y)
                            grab = Qt.point(at.x - (frame.cropLeft + handle.modelData[0] * frame.cropWidth),
                                            at.y - (frame.cropTop + handle.modelData[1] * frame.cropHeight))
                        }
                        onPositionChanged: (mouse) => {
                            const at = mapToItem(frame, mouse.x, mouse.y)
                            frame.dragCorner(handle.modelData[0], handle.modelData[1],
                                             (at.x - grab.x) / frame.width, (at.y - grab.y) / frame.height)
                        }
                        onReleased: editor.commit()
                    }
                }
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

        Segmented {
            id: tabs
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 16
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            options: [{ value: "adjust", label: qsTr("Adjust") }, { value: "looks", label: qsTr("Looks") }]
            value: view.tab
            onPicked: (value) => view.tab = value
        }

        Flickable {
            id: scroll
            anchors.top: tabs.bottom
            anchors.bottom: footer.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 16
            anchors.bottomMargin: 8
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            clip: true
            contentHeight: view.tab === "adjust" ? adjust.height : looks.height
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: adjust
                visible: view.tab === "adjust"
                width: scroll.width
                spacing: 24

                Column {
                    width: parent.width
                    spacing: 12
                    Heading { text: qsTr("Crop") }
                    ChipGrid {
                        width: parent.width
                        columns: 5
                        options: [{ value: "free", label: qsTr("Free") }, { value: "1:1", label: "1:1" }, { value: "3:2", label: "3:2" },
                                  { value: "4:3", label: "4:3" }, { value: "16:9", label: "16:9" }]
                        value: editor.aspect
                        onPicked: (value) => editor.setAspect(value)
                    }
                    Grid {
                        id: tools
                        width: parent.width
                        columns: 4
                        columnSpacing: 4
                        readonly property real cell: (width - columnSpacing * 3) / 4
                        IconButton { tile: true; width: tools.cell; glyph: "rotate-left"; text: qsTr("Rotate left"); onClicked: editor.rotate(-1) }
                        IconButton { tile: true; width: tools.cell; glyph: "rotate-right"; text: qsTr("Rotate right"); onClicked: editor.rotate(1) }
                        IconButton { tile: true; width: tools.cell; glyph: "flip"; text: qsTr("Flip sideways"); onClicked: editor.flip(true) }
                        IconButton { tile: true; width: tools.cell; glyph: "flip-up"; text: qsTr("Flip upside down"); onClicked: editor.flip(false) }
                    }
                    SliderRow {
                        width: parent.width
                        label: qsTr("Straighten")
                        from: -45; to: 45; step: 0.5
                        value: editor.angle
                        display: value.toLocaleString(Qt.locale(), "f", 1) + "°"
                        onMoved: (value) => editor.setAngle(value)
                        onReleased: editor.commit()
                    }
                }

                Repeater {
                    model: [
                        { title: qsTr("Light"), rows: [{ key: "exposure", label: qsTr("Exposure"), from: -100 },
                                                       { key: "contrast", label: qsTr("Contrast"), from: -100 },
                                                       { key: "fade", label: qsTr("Fade"), from: 0 }] },
                        { title: qsTr("Color"), rows: [{ key: "saturation", label: qsTr("Saturation"), from: -100 },
                                                       { key: "warmth", label: qsTr("Warmth"), from: -100 },
                                                       { key: "tint", label: qsTr("Tint"), from: -100 }] },
                        { title: qsTr("Finish"), rows: [{ key: "grain", label: qsTr("Grain"), from: 0 },
                                                        { key: "vignette", label: qsTr("Vignette"), from: 0 }] }
                    ]
                    Column {
                        id: section
                        required property var modelData
                        width: adjust.width
                        spacing: 6
                        Heading { text: section.modelData.title; bottomPadding: 4 }
                        Repeater {
                            model: section.modelData.rows
                            SliderRow {
                                required property var modelData
                                objectName: "slider-" + modelData.key
                                width: section.width
                                label: modelData.label
                                from: modelData.from
                                value: editor.values[modelData.key] ?? 0
                                display: (value > 0 && from < 0 ? "+" : "") + Math.round(value)
                                onMoved: (value) => editor.set(modelData.key, value)
                                onReleased: editor.commit()
                            }
                        }
                    }
                }
            }

            Column {
                id: looks
                visible: view.tab === "looks"
                width: scroll.width
                spacing: 16

                ChipGrid {
                    width: parent.width
                    columns: 3
                    options: editor.groups.map((group) => ({ value: group.name, label: group.name, note: String(group.count) }))
                    value: view.group
                    onPicked: (value) => view.group = value
                }
                Grid {
                    id: grid
                    width: parent.width
                    columns: 3
                    columnSpacing: 8
                    rowSpacing: 12
                    Repeater {
                        model: editor.looks.filter((look) => look.group === view.group)
                        AbstractButton {
                            id: tile
                            required property var modelData
                            readonly property bool picked: editor.look === modelData.name
                            objectName: "look-" + modelData.name
                            width: (grid.width - 16) / 3
                            height: thumb.height + 22
                            focusPolicy: Qt.NoFocus
                            Accessible.name: modelData.name
                            // A second click on the look in use takes it off.
                            onClicked: editor.setLook(picked ? "" : modelData.name)
                            background: null
                            contentItem: Item {
                                Rectangle {
                                    id: thumb
                                    width: parent.width
                                    height: Math.round(width * 0.64)
                                    radius: Math.min(4, theme.radius)
                                    color: Qt.alpha(theme.foreground, 0.06)
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        // The picked ring is drawn inside the tile, so it never
                                        // reaches into the gap or the tile beside it.
                                        anchors.margins: tile.picked ? 4 : 0
                                        fillMode: Image.PreserveAspectCrop
                                        cache: false
                                        source: editor.thumbRevision > 0
                                            ? "image://made/mem/" + editor.thumbRevision + "/look/" + tile.modelData.index : ""
                                    }
                                }
                                Rectangle {
                                    visible: tile.picked
                                    anchors.fill: thumb
                                    radius: thumb.radius
                                    color: "transparent"
                                    border.width: 2
                                    border.color: theme.accent
                                }
                                Text {
                                    anchors.top: thumb.bottom
                                    anchors.topMargin: 6
                                    width: parent.width
                                    text: tile.modelData.name
                                    font.pixelSize: 11
                                    font.weight: tile.picked ? Font.DemiBold : Font.Normal
                                    color: theme.foreground
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
                SliderRow {
                    width: parent.width
                    labelWidth: 76
                    enabled: editor.look.length > 0
                    label: qsTr("Strength")
                    from: 0; to: 100; resting: 0
                    value: editor.strength
                    onMoved: (value) => editor.setStrength(value)
                    onReleased: editor.commit()
                }
            }
        }

        RowLayout {
            id: footer
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 16
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 10
            PlainButton {
                focusPolicy: Qt.NoFocus
                text: view.tab === "adjust" ? qsTr("Reset all") : qsTr("No look")
                enabled: view.tab === "adjust" ? editor.dirty : editor.look.length > 0
                onClicked: view.tab === "adjust" ? editor.reset() : editor.setLook("")
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: qsTr("Hold \\ for the original")
                font.pixelSize: 11
                color: Qt.alpha(theme.foreground, 0.62)
                elide: Text.ElideLeft
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: win
    width: 1100
    height: 720
    minimumWidth: 640
    minimumHeight: 440
    visible: true
    title: backend.name.length > 0 ? backend.name + " · Diapo" : "Diapo"
    color: theme.background

    Material.theme: theme.light ? Material.Light : Material.Dark
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground

    readonly property bool rightToLeft: Qt.application.layoutDirection === Qt.RightToLeft
    // What sits behind a photo: a shade under the window's own color.
    readonly property color well: theme.light ? Qt.darker(theme.background, 1.06) : Qt.darker(theme.background, 1.45)

    // "view", "edit" or "compress": one window, one of three jobs at a time.
    property string mode: "view"
    property bool detailsOpen: false
    readonly property bool asking: ask.opened || help.opened || saveDialog.opened
    readonly property bool viewing: mode === "view" && !asking
    readonly property bool hasPhoto: backend.path.length > 0

    // Hyprland sizes a new window only once it is on screen, so the content
    // waits for that size rather than being laid out twice, as Files does.
    property bool settled: false
    onWidthChanged: if (visible) settled = true
    onHeightChanged: if (visible) settled = true
    Timer { interval: 150; running: true; onTriggered: win.settled = true }

    function startScreen() {
        backend.mark("switching to " + startIn)
        // Several at once with +, such as edit+shortcuts.
        const steps = startIn.split("+")
        if (steps.includes("edit") || steps.includes("looks"))
            edit(steps.includes("edit") ? "adjust" : "looks")
        else if (steps.includes("compress"))
            compress()
        if (steps.includes("shortcuts"))
            help.open()
        // looks+pick also puts the second look on, to see the picked outline.
        if (steps.includes("pick"))
            editor.setLook("Contemporary")
    }
    Timer { interval: Math.max(1, startAfter); running: startIn.length > 0 && startIn !== "step"; onTriggered: win.startScreen() }
    // DIAPO_OPEN=step: steps to the next photo every DIAPO_OPEN_AFTER ms, for timing.
    Timer { interval: Math.max(100, startAfter); repeat: true; running: startIn === "step"; onTriggered: backend.next() }

    function edit(tab) {
        if (!hasPhoto || backend.animated || mode !== "view")
            return
        editView.tab = tab
        editor.begin(backend.path)
        mode = "edit"
    }
    function leaveEdit() {
        editor.end()
        mode = "view"
    }
    // Leaving with edits made asks first; dismissing the question keeps them.
    function cancelEdit(then) {
        if (!editor.dirty) {
            leaveEdit()
            if (then)
                then()
            return
        }
        ask.ask(qsTr("Discard your edits?"), qsTr("The photo itself has not been changed."), qsTr("Discard"), true, () => {
            leaveEdit()
            if (then)
                then()
        })
    }
    function compress() {
        if (!hasPhoto || mode !== "view")
            return
        compressor.begin([backend.path])
        mode = "compress"
    }
    function leaveCompress() {
        saveDialog.close()
        compressor.end()
        mode = "view"
    }
    function quit() {
        if (mode === "edit")
            cancelEdit(Qt.quit)
        else
            Qt.quit()
    }
    function trash() {
        if (hasPhoto)
            ask.ask(qsTr("Move this photo to the trash?"), backend.name, qsTr("Move to trash"), true, () => backend.trash())
    }

    Shortcut { sequences: ["?", "F1"]; enabled: !saveDialog.opened; onActivated: help.opened ? help.close() : help.open() }
    Shortcut { sequences: ["Q", "Ctrl+Q", "Ctrl+W"]; enabled: !win.asking; onActivated: win.quit() }
    Shortcut { sequence: "Ctrl+O"; enabled: !win.asking && win.mode !== "edit"; onActivated: win.mode === "compress" ? compressor.addPhotos() : backend.pickPhoto() }
    Shortcut { sequences: ["F", "F11"]; enabled: !win.asking; onActivated: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }

    Shortcut { sequences: ["Space", "Right", "PgDown"]; enabled: win.viewing; onActivated: backend.next() }
    Shortcut { sequences: ["Backspace", "Left", "PgUp"]; enabled: win.viewing; onActivated: backend.previous() }
    Shortcut { sequence: "Home"; enabled: win.viewing; onActivated: backend.first() }
    Shortcut { sequence: "End"; enabled: win.viewing; onActivated: backend.last() }
    Shortcut { sequence: "E"; enabled: win.viewing; onActivated: win.edit("adjust") }
    Shortcut { sequence: "L"; enabled: win.viewing; onActivated: win.edit("looks") }
    Shortcut { sequence: "C"; enabled: win.viewing; onActivated: win.compress() }
    Shortcut { sequence: "I"; enabled: win.viewing; onActivated: win.detailsOpen = !win.detailsOpen }
    Shortcut { sequences: ["+", "="]; enabled: win.viewing; onActivated: photo.zoomBy(1.25) }
    Shortcut { sequence: "-"; enabled: win.viewing; onActivated: photo.zoomBy(0.8) }
    Shortcut { sequence: "0"; enabled: win.viewing; onActivated: photo.fit() }
    Shortcut { sequence: "1"; enabled: win.viewing; onActivated: photo.actualSize() }
    Shortcut { sequence: "Z"; enabled: win.viewing; onActivated: photo.fill = !photo.fill }
    Shortcut { sequence: "Delete"; enabled: win.viewing; onActivated: win.trash() }
    Shortcut { sequence: "Ctrl+C"; enabled: win.viewing; onActivated: backend.copy() }
    Shortcut {
        sequence: "Escape"
        enabled: !win.asking
        onActivated: {
            if (win.mode === "edit")
                win.cancelEdit(null)
            else if (win.mode === "compress")
                win.leaveCompress()
            else if (win.visibility === Window.FullScreen)
                win.visibility = Window.Windowed
            else if (win.detailsOpen)
                win.detailsOpen = false
        }
    }

    Shortcut { sequence: "Ctrl+S"; enabled: win.mode === "edit" && !win.asking; onActivated: editor.save() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: win.mode === "edit" && !win.asking; onActivated: editor.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: win.mode === "edit" && !win.asking; onActivated: editor.redo() }
    Shortcut { sequence: "["; enabled: win.mode === "edit" && !win.asking; onActivated: editor.rotate(-1) }
    Shortcut { sequence: "]"; enabled: win.mode === "edit" && !win.asking; onActivated: editor.rotate(1) }
    Shortcut { sequences: ["Return", "Enter"]; enabled: win.mode === "compress" && !win.asking; onActivated: compressor.resultReady ? saveDialog.open() : compressor.compress() }

    Connections {
        target: editor
        function onSaved(path) {
            win.leaveEdit()
            backend.open(path)
            backend.say(qsTr("Saved as %1").arg(backend.name))
        }
        function onFailed(message) { backend.say(message) }
    }
    Connections {
        target: compressor
        // The popup comes up by itself the moment compressing is done.
        function onResultChanged() {
            if (compressor.resultReady && !compressor.running && win.mode === "compress")
                saveDialog.open()
        }
        function onSaved(firstPath, count) {
            const folder = backend.tidyPath(compressor.folder)
            win.leaveCompress()
            backend.say(count === 1 ? qsTr("Saved to %1").arg(folder) : qsTr("%Ln photos saved to %1", "", count).arg(folder))
        }
        function onFailed(message) { backend.say(message) }
    }
    Connections {
        target: backend
        function onNoticeChanged() {
            toast.shown = backend.notice.length > 0
            toastTimer.restart()
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        enabled: win.settled
        opacity: win.settled ? 1 : 0
        LayoutMirroring.enabled: win.rightToLeft
        LayoutMirroring.childrenInherit: true

        Item {
            id: viewer
            anchors.fill: parent
            visible: win.mode === "view"

            TopBar {
                id: bar
                width: parent.width
                // Not tied to whether the viewer is on show, or hiding it behind
                // the editor would make the photo area taller and ask for a
                // new decode nobody sees.
                visible: win.visibility !== Window.FullScreen
                height: win.visibility !== Window.FullScreen ? 60 : 0
                title: win.hasPhoto ? backend.name : "Diapo"
                subtitle: {
                    if (!win.hasPhoto)
                        return qsTr("A photo viewer")
                    const parts = [backend.kind, backend.formatBytes(backend.bytes)]
                    if (backend.size.width > 0)
                        parts.push(backend.size.width + " × " + backend.size.height)
                    if (photo.zoom > 1)
                        parts.push(Math.round(photo.scale * 100) + "%")
                    return parts.join(" · ")
                }

                Text {
                    objectName: "position"
                    visible: backend.count > 1
                    Layout.rightMargin: 8
                    text: qsTr("%1 of %2").arg(backend.index + 1).arg(backend.count)
                    color: Qt.alpha(theme.foreground, 0.62)
                    font.pixelSize: 12
                }
                IconButton { glyph: "back"; text: qsTr("Previous photo"); enabled: backend.count > 1; onClicked: backend.previous() }
                IconButton { glyph: "forward"; text: qsTr("Next photo"); enabled: backend.count > 1; onClicked: backend.next() }
                BarLine {}
                IconButton { objectName: "editButton"; glyph: "edit"; text: qsTr("Edit"); enabled: win.hasPhoto && !backend.animated; onClicked: win.edit("adjust") }
                IconButton { glyph: "looks"; text: qsTr("Looks"); enabled: win.hasPhoto && !backend.animated; onClicked: win.edit("looks") }
                IconButton { glyph: "compress"; text: qsTr("Compress"); enabled: win.hasPhoto; onClicked: win.compress() }
                IconButton { glyph: "details"; text: qsTr("Details"); enabled: win.hasPhoto; active: win.detailsOpen; onClicked: win.detailsOpen = !win.detailsOpen }
                BarLine {}
                IconButton { glyph: "keyboard"; text: qsTr("Keyboard shortcuts"); onClicked: help.open() }
                IconButton { glyph: "close"; text: qsTr("Close"); onClicked: win.quit() }
            }

            Rectangle {
                anchors.top: bar.bottom
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: details.visible ? details.left : parent.right
                color: win.well

                PhotoView {
                    id: photo
                    anchors.fill: parent
                    visible: win.hasPhoto
                }
                Column {
                    anchors.centerIn: parent
                    visible: !win.hasPhoto
                    spacing: 16
                    Glyph { anchors.horizontalCenter: parent.horizontalCenter; name: "picture"; size: 40; stroke: 1.2; color: Qt.alpha(theme.foreground, 0.4) }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Drop a photo here, or press Ctrl+O")
                        color: Qt.alpha(theme.foreground, 0.62)
                    }
                    PlainButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Open a photo")
                        onClicked: backend.pickPhoto()
                    }
                }
            }

            Rectangle {
                id: details
                visible: win.detailsOpen && win.hasPhoto
                anchors.top: bar.bottom
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: Math.min(300, parent.width * 0.42)
                color: theme.background

                function label(id) {
                    switch (id) {
                    case "name": return qsTr("Name")
                    case "folder": return qsTr("Folder")
                    case "kind": return qsTr("Kind")
                    case "bytes": return qsTr("Size")
                    case "pixels": return qsTr("Pixels")
                    case "changed": return qsTr("Changed")
                    case "camera": return qsTr("Camera")
                    case "lens": return qsTr("Lens")
                    case "taken": return qsTr("Taken")
                    case "exposure": return qsTr("Shutter")
                    case "aperture": return qsTr("Aperture")
                    case "iso": return qsTr("Sensitivity")
                    default: return qsTr("Focal length")
                    }
                }

                Rectangle { width: 1 / Screen.devicePixelRatio; height: parent.height; color: Qt.alpha(theme.foreground, 0.08) }
                Flickable {
                    anchors.fill: parent
                    anchors.margins: 20
                    clip: true
                    contentHeight: facts.height
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: facts
                        width: parent.width
                        spacing: 14
                        Repeater {
                            model: backend.details
                            Column {
                                required property var modelData
                                width: facts.width
                                spacing: 4
                                Heading { text: details.label(modelData.id) }
                                Text {
                                    width: parent.width
                                    text: modelData.value
                                    color: theme.foreground
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                        }
                        PlainButton {
                            text: qsTr("Show in Files")
                            onClicked: backend.reveal()
                        }
                    }
                }
            }
        }

        EditView {
            id: editView
            anchors.fill: parent
            visible: win.mode === "edit"
            onCancel: win.cancelEdit(null)
            onShowShortcuts: help.open()
        }

        CompressView {
            anchors.fill: parent
            visible: win.mode === "compress"
            onLeave: win.leaveCompress()
            onAskSave: saveDialog.open()
            onShowShortcuts: help.open()
        }

        DropArea {
            anchors.fill: parent
            enabled: win.mode !== "edit"
            onDropped: (drop) => {
                if (!drop.hasUrls)
                    return
                if (win.mode === "compress")
                    compressor.addUrls(drop.urls)
                else
                    backend.open(drop.urls[0])
            }
        }

        Rectangle {
            id: toast
            property bool shown: false
            visible: shown
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            width: Math.min(parent.width - 40, toastText.implicitWidth + 28)
            height: 36
            radius: Math.min(4, theme.radius)
            color: theme.background
            border.color: Qt.alpha(theme.foreground, 0.16)
            Text {
                id: toastText
                objectName: "toast"
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                text: backend.notice
                color: theme.foreground
                elide: Text.ElideMiddle
            }
            Timer { id: toastTimer; interval: 3500; onTriggered: toast.shown = false }
        }
    }

    AskDialog { id: ask; objectName: "ask" }
    SaveDialog { id: saveDialog; objectName: "saveDialog" }

    ShortcutsPopup {
        id: help
        objectName: "help"
        mode: win.mode
    }
}

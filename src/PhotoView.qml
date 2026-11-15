import QtQuick
import QtQuick.Window

// The photo, fitted to the whole area, with zoom and pan. A size that fits
// the screen is decoded first and shown at once; the full picture is decoded
// only when zooming in asks for more.
Item {
    id: view
    clip: true

    readonly property size natural: backend.size.width > 0 ? backend.size
                                    : Qt.size(Math.max(1, fitted.implicitWidth), Math.max(1, fitted.implicitHeight))
    // Small pictures are shown at their own size rather than blown up.
    // Filling covers the whole area and lets the edges go; fitting shows it all.
    property bool fill: false
    readonly property real fitScale: Math.min(fill ? Math.max(width / natural.width, height / natural.height)
                                                   : Math.min(width / natural.width, height / natural.height), 1)
    onFillChanged: clampPan()
    property real zoom: 1
    readonly property real scale: fitScale * zoom
    readonly property real maxZoom: Math.max(1, 8 / fitScale)
    property real panX: 0
    property real panY: 0
    // The longest side the photo has on screen, in device pixels: it is
    // decoded at exactly that, so one picture pixel lands on one screen pixel.
    readonly property real dpr: Screen.devicePixelRatio
    readonly property int longest: Math.max(natural.width, natural.height)
    // Nothing is asked for until the view has its real size, so a photo is
    // never decoded for the moment the window is still a few pixels wide.
    readonly property bool sized: width > 64 && height > 64 && win.settled && visible
    readonly property int wanted: !sized ? 0
        : backend.size.width > 0 ? Math.max(1, Math.min(longest, Math.ceil(longest * fitScale * dpr))) : 2560
    // Photos decoded ahead are all made at one size, the view's longest side,
    // whatever their shape, so each is big enough for the photo it turns out
    // to be and is only scaled down when shown, never decoded again.
    readonly property int ahead: Math.ceil(Math.max(width, height) * dpr)
    property int quick: 0
    onQuickChanged: backend.mark("view asks for " + quick + " px (" + Math.round(width) + "×" + Math.round(height) + ")")
    onWantedChanged: quick === 0 ? quick = wanted : (wanted > 0 ? settle.restart() : settle.stop())
    // A window being dragged to a new size asks once, when it rests.
    Timer { id: settle; interval: 160; onTriggered: view.quick = view.wanted }
    property bool wantFull: false
    readonly property bool failed: fitted.status === Image.Error && !backend.animated
    readonly property bool loading: backend.path.length > 0 && !backend.animated && fitted.status !== Image.Ready && !failed
    // The photo asked for is not drawn yet; set when it changes, cleared when
    // its own picture (not the last photo's, kept meanwhile) is ready.
    property bool waiting: false

    function clampPan() {
        const overX = Math.max(0, (natural.width * scale - width) / 2)
        const overY = Math.max(0, (natural.height * scale - height) / 2)
        panX = Math.max(-overX, Math.min(overX, panX))
        panY = Math.max(-overY, Math.min(overY, panY))
    }
    // Zooms about a point of the view, so what is under the pointer stays there.
    function zoomAt(factor, x, y) {
        const next = Math.max(1, Math.min(maxZoom, zoom * factor))
        const ratio = next / zoom
        const dx = x - width / 2 - panX, dy = y - height / 2 - panY
        panX -= dx * (ratio - 1)
        panY -= dy * (ratio - 1)
        zoom = next
        clampPan()
        if (quick > 0 && longest * scale * dpr > quick * 1.01)
            wantFull = true
    }
    function zoomBy(factor) { zoomAt(factor, width / 2, height / 2) }
    function fit() { zoom = 1; panX = 0; panY = 0 }
    function actualSize() { zoomAt(1 / (fitScale * zoom), width / 2, height / 2) }

    onWidthChanged: clampPan()
    onHeightChanged: clampPan()
    Connections {
        target: backend
        function onPhotoChanged() {
            view.waiting = true
            view.wantFull = false
            view.fit()
            settle.stop()
            view.quick = view.wanted
        }
    }

    Item {
        id: canvas
        width: Math.round(view.natural.width * view.scale * view.dpr) / view.dpr
        height: Math.round(view.natural.height * view.scale * view.dpr) / view.dpr
        x: Math.round(((view.width - width) / 2 + view.panX) * view.dpr) / view.dpr
        y: Math.round(((view.height - height) / 2 + view.panY) * view.dpr) / view.dpr

        // A small picture of the photo (a HEIC's own preview, or Files'
        // thumbnail) stands in while it decodes, so stepping is never stuck
        // on the last photo.
        Image {
            id: standIn
            anchors.fill: parent
            readonly property bool real: status === Image.Ready && implicitWidth > 1
            visible: view.waiting && real
            onRealChanged: if (real && view.waiting) backend.mark("stand-in shown: " + backend.name)
            asynchronous: true
            cache: false
            smooth: true
            fillMode: Image.PreserveAspectFit
            source: backend.path && !backend.animated ? "image://photo/thumb/" + backend.token : ""
        }
        Image {
            id: fitted
            anchors.fill: parent
            visible: !backend.animated
            asynchronous: true
            cache: false
            // The last photo stays up until the next is decoded, in its own
            // proportions: the frame already has the next photo's shape, and
            // stretching the old one to it is what made it bulge for a moment.
            retainWhileLoading: true
            fillMode: Image.PreserveAspectFit
            // The last photo gives way to the stand-in once that is there.
            opacity: view.waiting && standIn.real ? 0 : 1
            mipmap: false
            source: backend.path && !backend.animated && view.quick > 0 ? "image://photo/file/" + view.quick + "/" + backend.token : ""
            onStatusChanged: if (status === Image.Ready) {
                view.waiting = false
                backend.mark("photo shown at " + view.quick + " px: " + backend.name)
                backend.prefetchAround(view.ahead)
            }
        }
        Image {
            anchors.fill: parent
            visible: status === Image.Ready
            asynchronous: true
            cache: false
            mipmap: true
            smooth: view.scale < 3
            fillMode: Image.PreserveAspectFit
            source: view.wantFull && backend.path && !backend.animated ? "image://photo/file/0/" + backend.token : ""
        }
        // Created only for a moving picture, which plays from its file.
        Loader {
            anchors.fill: parent
            active: backend.animated
            sourceComponent: AnimatedImage {
                fillMode: Image.PreserveAspectFit
                source: backend.url
                cache: false
            }
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        visible: view.loading && loadingDelay.passed
        text: qsTr("Opening…")
        font.pixelSize: 11
        color: Qt.alpha(theme.foreground, 0.62)
    }
    // Said only when it takes long enough to notice.
    Timer {
        id: loadingDelay
        property bool passed: false
        interval: 400
        running: view.loading
        onRunningChanged: if (running) passed = false
        onTriggered: passed = true
    }
    Text {
        anchors.centerIn: parent
        visible: view.failed
        text: qsTr("This photo could not be opened.")
        color: Qt.alpha(theme.foreground, 0.62)
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        property real lastX: 0
        property real lastY: 0
        cursorShape: view.zoom > 1 ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor
        onPressed: (mouse) => { lastX = mouse.x; lastY = mouse.y }
        onPositionChanged: (mouse) => {
            if (view.zoom <= 1)
                return
            view.panX += mouse.x - lastX
            view.panY += mouse.y - lastY
            lastX = mouse.x
            lastY = mouse.y
            view.clampPan()
        }
        onDoubleClicked: (mouse) => view.zoom > 1 ? view.fit() : view.zoomAt(1 / view.fitScale, mouse.x, mouse.y)
        onWheel: (wheel) => {
            if (wheel.angleDelta.y !== 0)
                view.zoomAt(Math.pow(1.0015, wheel.angleDelta.y), wheel.x, wheel.y)
        }
    }
}

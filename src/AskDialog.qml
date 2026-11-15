import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A question with one answer that does something and one that loses nothing,
// which is also what Escape gives.
Popup {
    id: dialog
    property string title
    property string detail
    property string confirm
    property bool destructive: false
    property var action: null

    function ask(title, detail, confirm, destructive, action) {
        dialog.title = title
        dialog.detail = detail
        dialog.confirm = confirm
        dialog.destructive = destructive
        dialog.action = action
        open()
    }

    anchors.centerIn: Overlay.overlay
    modal: true
    focus: true
    padding: 24
    enter: null
    exit: null
    background: Rectangle {
        color: theme.background
        border.color: Qt.alpha(theme.foreground, 0.16)
        radius: theme.radius
    }
    onOpened: yes.forceActiveFocus()

    ColumnLayout {
        spacing: 10
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
        Text {
            text: dialog.title
            color: theme.foreground
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Text {
            Layout.maximumWidth: 380
            visible: text.length > 0
            text: dialog.detail
            color: Qt.alpha(theme.foreground, 0.62)
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.topMargin: 12
            Layout.alignment: Qt.AlignRight
            spacing: 10
            PlainButton {
                objectName: "askNo"
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }
            PlainButton {
                id: yes
                objectName: "askYes"
                text: dialog.confirm
                primary: !dialog.destructive
                destructive: dialog.destructive
                onClicked: {
                    const action = dialog.action
                    dialog.close()
                    if (action)
                        action()
                }
            }
        }
    }
}

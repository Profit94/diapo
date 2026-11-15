import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shown the moment compressing finishes: what was gained, and where to put it.
// Nothing is written until Save; Escape goes back with the result kept.
Popup {
    id: dialog

    anchors.centerIn: Overlay.overlay
    width: Math.min(460, Overlay.overlay ? Overlay.overlay.width - 48 : 460)
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

    readonly property bool one: compressor.resultCount === 1
    readonly property int saving: compressor.resultOriginalBytes > 0
        ? Math.round(100 * (1 - compressor.resultBytes / compressor.resultOriginalBytes)) : 0

    function save() {
        if (compressor.resultReady)
            compressor.save(nameField.text)
    }
    // The name offered for the folder in use. A new folder offers its own
    // (the old name may be taken there), unless a name was typed meanwhile.
    property string offered
    onAboutToShow: nameField.text = offered = compressor.suggestedName
    Connections {
        target: compressor
        function onResultChanged() {
            if (dialog.visible && nameField.text === dialog.offered && compressor.suggestedName.length > 0)
                nameField.text = dialog.offered = compressor.suggestedName
        }
    }
    onOpened: one ? nameField.forceActiveFocus() : saveButton.forceActiveFocus()

    ColumnLayout {
        anchors.fill: parent
        spacing: 16
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            spacing: 10
            Glyph { name: "check"; color: theme.colors.green }
            Text {
                text: qsTr("Compressed. Save it?")
                color: theme.foreground
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 80
            radius: Math.min(6, theme.radius)
            color: Qt.alpha(theme.foreground, 0.05)
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 14
                Image {
                    Layout.preferredWidth: 84
                    Layout.preferredHeight: 56
                    fillMode: Image.PreserveAspectCrop
                    cache: false
                    source: dialog.visible ? "image://made/mem/" + compressor.revision + "/cmp-b" : ""
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    RowLayout {
                        spacing: 8
                        Text {
                            text: backend.formatBytes(compressor.resultOriginalBytes) + "  →"
                            color: Qt.alpha(theme.foreground, 0.62)
                        }
                        Text {
                            objectName: "savedSize"
                            text: backend.formatBytes(compressor.resultBytes)
                            color: theme.foreground
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 11
                        color: Qt.alpha(theme.foreground, 0.62)
                        textFormat: Text.StyledText
                        text: {
                            const gain = dialog.saving >= 0
                                ? "<font color=\"" + theme.colors.green + "\">" + qsTr("%1% smaller").arg(dialog.saving) + "</font>"
                                : qsTr("%1% larger").arg(-dialog.saving)
                            return gain + " · " + (dialog.one
                                ? compressor.resultFormat + " · " + compressor.resultSize.width + " × " + compressor.resultSize.height
                                : qsTr("%Ln photos", "", compressor.resultCount))
                        }
                    }
                }
            }
        }

        ColumnLayout {
            visible: dialog.one
            Layout.fillWidth: true
            spacing: 8
            Heading { text: qsTr("Name") }
            TextField {
                id: nameField
                objectName: "saveName"
                Layout.fillWidth: true
                font.pixelSize: 12
                color: theme.foreground
                selectionColor: theme.accent
                selectedTextColor: theme.accentForeground
                leftPadding: 12
                rightPadding: 12
                topPadding: 9
                bottomPadding: 9
                selectByMouse: true
                Accessible.name: qsTr("Name")
                onAccepted: dialog.save()
                background: Rectangle {
                    implicitHeight: 36
                    color: Qt.alpha(theme.foreground, 0.05)
                    border.width: 1
                    border.color: nameField.activeFocus ? theme.accent : Qt.alpha(theme.foreground, 0.18)
                    radius: Math.min(4, theme.radius)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            Heading { text: qsTr("Folder") }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 36
                    color: Qt.alpha(theme.foreground, 0.05)
                    border.width: 1
                    border.color: Qt.alpha(theme.foreground, 0.18)
                    radius: Math.min(4, theme.radius)
                    Glyph {
                        id: folderGlyph
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        name: "folder"
                        color: Qt.alpha(theme.foreground, 0.62)
                    }
                    Text {
                        objectName: "saveFolder"
                        anchors.left: folderGlyph.right
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: backend.tidyPath(compressor.folder)
                        color: theme.foreground
                        elide: Text.ElideMiddle
                    }
                }
                PlainButton {
                    objectName: "saveChange"
                    text: qsTr("Change…")
                    onClicked: compressor.chooseFolder()
                }
            }
            Text {
                text: qsTr("The original stays as it is.")
                font.pixelSize: 11
                color: Qt.alpha(theme.foreground, 0.62)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: qsTr("Enter saves · Esc goes back")
                font.pixelSize: 11
                color: Qt.alpha(theme.foreground, 0.62)
                elide: Text.ElideRight
            }
            PlainButton {
                objectName: "saveLater"
                text: qsTr("Not now")
                onClicked: dialog.close()
            }
            PlainButton {
                id: saveButton
                objectName: "saveNow"
                text: qsTr("Save")
                primary: true
                onClicked: dialog.save()
            }
        }
    }
}

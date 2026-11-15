import QtQuick

// The small capital label over a group of controls, in a soft tint of the
// theme's accent so the groups read apart without shouting.
Text {
    color: Qt.tint(Qt.alpha(theme.foreground, 0.62), Qt.alpha(theme.accent, 0.55))
    font.pixelSize: 11
    font.weight: Font.DemiBold
    font.capitalization: Font.AllUppercase
    font.letterSpacing: 1
}

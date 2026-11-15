import QtQuick

// A few choices in even columns, so every row lines up with the one above.
Grid {
    id: grid
    // [{ value, label, note }]
    property var options: []
    property var value
    signal picked(var value)

    columnSpacing: 4
    rowSpacing: 4

    Repeater {
        model: grid.options
        Chip {
            required property var modelData
            objectName: "chip-" + modelData.value
            width: (grid.width - grid.columnSpacing * (grid.columns - 1)) / grid.columns
            text: modelData.label
            note: modelData.note ?? ""
            picked: grid.value === modelData.value
            onClicked: grid.picked(modelData.value)
        }
    }
}

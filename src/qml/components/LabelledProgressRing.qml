import QtQuick
import Drift

// Large progress ring with the percentage in the middle.
//
// The export and subtitle progress dialogs each open-coded this same block.
Item {
    id: root

    // 0..1
    property real value: 0
    // Spins instead of showing a fraction (work started, no progress reported yet).
    property bool indeterminate: false
    property real ringSize: 100
    property real strokeWidth: 7

    implicitWidth: ringSize
    implicitHeight: ringSize
    height: ringSize

    CircularProgress {
        anchors.centerIn: parent
        size: root.ringSize
        strokeWidth: root.strokeWidth
        value: root.value
        indeterminate: root.indeterminate
    }

    ThemedLabel {
        anchors.centerIn: parent
        tone: "default"
        size: "base"
        visible: !root.indeterminate
        text: Math.round(root.value * 100) + "%"
    }
}

import QtQuick
import Drift

// Shared style-pack thumbnail: dark canvas so light glyphs stay legible in both themes.
Item {
    id: root

    property string presetId: ""
    property bool selected: false
    property bool hovered: false

    // Landscape cards match the properties picker; assets can size freely.
    implicitWidth: 120
    implicitHeight: Math.round(implicitWidth * 0.5)

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm
        color: Theme.textStylePreviewBg
        border.width: root.selected ? Theme.borderWidthFocus : Theme.borderWidth
        border.color: root.selected ? Theme.primary
                                    : (root.hovered ? Theme.panelMuted : Theme.textStylePreviewBorder)
        clip: true

        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Image {
            anchors.fill: parent
            anchors.margins: 4
            visible: root.presetId.length > 0
            asynchronous: true
            fillMode: Image.Pad
            source: root.presetId.length > 0 ? ("image://textstyle/" + root.presetId) : ""
            sourceSize.width: Math.max(1, Math.round(width))
            sourceSize.height: Math.max(1, Math.round(height))
        }

        Text {
            anchors.centerIn: parent
            visible: root.presetId.length === 0
            text: qsTr("Custom")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}

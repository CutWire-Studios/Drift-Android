import QtQuick
import QtQuick.Effects
import Drift

// Renders a Lucide icon from resources/icons/<name>.png (white-mask PNGs
// rasterised from lucide-icons-1.25.0 SVGs; tinted via MultiEffect).
// `glyph` is the Lucide file name without extension.
Item {
    id: root

    property string glyph: ""
    property real iconSize: 16
    property color iconColor: Theme.mutedForeground
    // Set for loader glyphs, which otherwise render as a frozen circle.
    property bool spinning: false

    implicitWidth: iconSize
    implicitHeight: iconSize

    Image {
        id: iconImage
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        source: root.glyph.length > 0
                ? "qrc:/qt/qml/Drift/resources/icons/" + root.glyph + ".png"
                : ""
        sourceSize: Qt.size(Math.ceil(root.iconSize * 2), Math.ceil(root.iconSize * 2))
        fillMode: Image.PreserveAspectFit
        visible: false
    }

    MultiEffect {
        id: tint
        anchors.fill: iconImage
        source: iconImage
        colorization: 1.0
        colorizationColor: root.iconColor

        // The MultiEffect *samples* iconImage, so rotating the Image has no
        // visible effect — the effect item itself is what has to turn.
        RotationAnimator {
            target: tint
            from: 0
            to: 360
            duration: 1100          // matches CircularProgress.qml
            loops: Animation.Infinite
            running: root.spinning && root.visible
        }
    }
}

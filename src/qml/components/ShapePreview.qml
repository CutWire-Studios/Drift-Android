import QtQuick
import QtQuick.Shapes
import Drift

// Thumbnail for the built-in shape clips.
//
// The path comes from the same drift::shapePath() the compositor rasterizes, serialized onto a
// fixed 0..100 grid, so a card can never show something the timeline does not render. The Shape is
// scaled to fit the item: drawn at raw coordinates it only rendered correctly at exactly 100×100.
Item {
    id: root

    required property string shapeKind

    // Authoring grid the path above is drawn on.
    readonly property real designSize: 100

    Item {
        // Square, centred, so shapes keep their aspect ratio in any container.
        readonly property real side: Math.min(root.width, root.height)
        width: root.designSize
        height: root.designSize
        anchors.centerIn: parent
        scale: side / root.designSize

        Shape {
            anchors.fill: parent
            antialiasing: true

            ShapePath {
                strokeColor: Theme.onMedia
                strokeWidth: 2
                // Shape clips are drawn with the graphic clip colour on the timeline, so the
                // preview stays in that family rather than an unrelated palette.
                fillColor: Theme.clipGraphic

                PathSvg { path: EditorState.shapeSvgPath(root.shapeKind) }
            }
        }
    }
}

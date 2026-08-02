import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Shapes tab. Shapes used to be a page inside the Stickers tab, where they came
// and went with the sticker addon and had no room for categories of their own.
Item {
    id: root

    readonly property var categories: EditorState.builtinShapeCategories()
    readonly property var allShapes: EditorState.builtinShapes()
    property int pageIndex: 0
    readonly property string currentCategoryId:
        categories[pageIndex] ? categories[pageIndex].id : ""

    Flow {
        id: shapePageBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        spacing: 6

        Repeater {
            model: root.categories
            delegate: ThemedChip {
                required property var modelData
                required property int index
                text: modelData.label
                variant: "secondary"
                selected: root.pageIndex === index
                onClicked: root.pageIndex = index
            }
        }
    }

    Flickable {
        anchors.top: shapePageBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: 12
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        contentHeight: shapeGrid.height + 24
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Grid {
            id: shapeGrid
            width: parent.width - 12
            columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
            columnSpacing: Theme.assetCardGap
            rowSpacing: Theme.assetCardGap

            Repeater {
                model: root.allShapes.filter(function(s) { return s.category === root.currentCategoryId })
                delegate: Column {
                    id: shapeCard
                    required property var modelData
                    width: Theme.assetCardWidth
                    spacing: Theme.spacingSm

                    Drag.active: shapeDrag.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.keys: ["application/x-drift-shape"]
                    Drag.mimeData: { "application/x-drift-shape": shapeCard.modelData.id }

                    Rectangle {
                        width: Theme.assetCardWidth
                        height: Theme.assetCardWidth
                        radius: Theme.radiusSm
                        color: shapeHover.hovered ? Theme.popoverHover : Theme.panelAccent

                        Behavior on color {
                            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        ShapePreview {
                            anchors.fill: parent
                            anchors.margins: Theme.pagePadding
                            shapeKind: shapeCard.modelData.id
                        }

                        HoverHandler {
                            id: shapeHover
                            cursorShape: Qt.PointingHandCursor
                        }

                        ThemedToolTip {
                            text: qsTr("%1 — click to add, or drag to the timeline").arg(shapeCard.modelData.label)
                            visible: shapeHover.hovered
                        }

                        TapHandler {
                            onTapped: EditorState.addShapeClip(shapeCard.modelData.id, -1)
                        }
                        DragHandler {
                            id: shapeDrag
                            target: null
                            acceptedButtons: Qt.LeftButton
                        }
                    }

                    Text {
                        width: parent.width
                        text: modelData.label
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeCard
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}

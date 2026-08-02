import QtQuick
import Drift
import "components"

// CapCut-style tool strip above the timeline: select/blade/delete/snap left, zoom right.
Item {
    id: root

    property var panel: null

    readonly property bool hasSelection: {
        void EditorState.selection
        return EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    }

    readonly property bool bladeActive: panel && panel.timelineTool === "split"

    height: Theme.androidEditActionsHeight
    width: parent ? parent.width : 0
    clip: true

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBackground
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.panelBorder
    }

    Flickable {
        anchors.left: parent.left
        anchors.right: zoomRow.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.spacingSm
        contentWidth: actionRow.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: actionRow
            height: parent.height
            leftPadding: Theme.spacingSm
            rightPadding: Theme.spacingSm
            spacing: Theme.spacingXs

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.mousePointer
                variant: "text"
                tooltip: qsTr("Select")
                active: root.panel && root.panel.timelineTool === ""
                onClicked: if (root.panel) root.panel.timelineTool = ""
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.scissors
                variant: "text"
                tooltip: qsTr("Blade — tap a clip to split")
                active: root.bladeActive
                onClicked: {
                    if (!root.panel)
                        return
                    root.panel.timelineTool =
                        root.panel.timelineTool === "split" ? "" : "split"
                }
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.trash
                variant: "text"
                tooltip: qsTr("Delete")
                enabled: root.hasSelection
                onClicked: EditorState.deleteSelectedClip()
            }

            Rectangle {
                width: Theme.borderWidth
                height: Theme.spacing3xl
                color: Theme.panelBorder
                anchors.verticalCenter: parent.verticalCenter
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.magnet
                variant: "text"
                tooltip: qsTr("Toggle snapping")
                active: EditorState.snapEnabled
                onClicked: EditorState.snapEnabled = !EditorState.snapEnabled
            }
        }
    }

    Row {
        id: zoomRow
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingXs

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            buttonSize: Theme.androidIconButtonSize
            iconSize: Theme.iconSizeLg
            glyph: Theme.icons.zoomOut
            variant: "text"
            tooltip: qsTr("Zoom out")
            enabled: !!root.panel
            onClicked: {
                if (!root.panel)
                    return
                root.panel.zoom = Math.max(root.panel.minZoom, root.panel.zoom / 1.5)
                root.panel.filmstripRefreshEpoch++
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 40
            text: root.panel ? (root.panel.zoom.toFixed(1) + "×") : "1.0×"
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeTick
            horizontalAlignment: Text.AlignHCenter
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (!root.panel)
                        return
                    root.panel.zoom = 1.0
                    root.panel.filmstripRefreshEpoch++
                }
            }
        }

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            buttonSize: Theme.androidIconButtonSize
            iconSize: Theme.iconSizeLg
            glyph: Theme.icons.zoomIn
            variant: "text"
            tooltip: qsTr("Zoom in")
            enabled: !!root.panel
            onClicked: {
                if (!root.panel)
                    return
                root.panel.zoom = Math.min(root.panel.maxZoom, root.panel.zoom * 1.5)
                root.panel.filmstripRefreshEpoch++
            }
        }
    }
}

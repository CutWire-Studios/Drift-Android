import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Compact CapCut-style timeline chrome for phones.
Item {
    id: toolbar

    property var panel
    // Optional: open a .drift project (wired from AndroidMain).
    property var openProjectCallback: null

    height: Theme.androidTimelineToolbarHeight
    width: parent ? parent.width : 0

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.panelBorder
    }

    Flickable {
        id: toolFlick
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingSm
        anchors.rightMargin: Theme.spacingSm
        contentWidth: toolRow.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: toolRow
            height: parent.height
            spacing: Theme.spacingXs

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.folder
                variant: "text"
                tooltip: qsTr("Open project")
                visible: toolbar.openProjectCallback !== null
                onClicked: {
                    if (toolbar.openProjectCallback)
                        toolbar.openProjectCallback()
                }
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.mousePointer
                variant: "text"
                tooltip: qsTr("Select")
                active: toolbar.panel.timelineTool === ""
                onClicked: toolbar.panel.timelineTool = ""
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.scissors
                variant: "text"
                tooltip: qsTr("Blade — tap a clip to split")
                active: toolbar.panel.timelineTool === "split"
                onClicked: toolbar.panel.timelineTool =
                           toolbar.panel.timelineTool === "split" ? "" : "split"
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.trash
                variant: "text"
                tooltip: qsTr("Delete")
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
                glyph: Theme.icons.undo
                variant: "text"
                tooltip: qsTr("Undo")
                enabled: EditorState.undoAvailable
                onClicked: EditorState.undo()
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.redo
                variant: "text"
                tooltip: qsTr("Redo")
                enabled: EditorState.redoAvailable
                onClicked: EditorState.redo()
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

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.zoomOut
                variant: "text"
                tooltip: qsTr("Zoom out")
                onClicked: toolbar.panel.zoom = Math.max(
                    toolbar.panel.minZoom, toolbar.panel.zoom / 1.5)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 40
                text: toolbar.panel.zoom.toFixed(1) + "×"
                color: Theme.mutedForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeTick
                horizontalAlignment: Text.AlignHCenter
                MouseArea {
                    anchors.fill: parent
                    onClicked: toolbar.panel.zoom = 1.0
                }
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.zoomIn
                variant: "text"
                tooltip: qsTr("Zoom in")
                onClicked: toolbar.panel.zoom = Math.min(
                    toolbar.panel.maxZoom, toolbar.panel.zoom * 1.5)
            }
        }
    }
}

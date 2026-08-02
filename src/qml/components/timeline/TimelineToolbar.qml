import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Timeline toolbar: transport/edit actions, scene badge, snap/ripple toggles
// and zoom controls. Zoom and the time readout are read from and written back
// to the owning TimelinePanel via `panel`. New tracks are added from the
// plus button above the track headers.
Item {
    id: toolbar

    // Owning TimelinePanel; provides zoom (read/write), zoom bounds and formatTime.
    property var panel

    height: Theme.timelineToolbarHeight

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.panelBorder
    }

    Row {
        id: leftControls
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingXs
        // Never runs under the right-hand controls; buttons past the
        // available width are clipped rather than overlapping.
        width: Math.max(0, rightControls.x - x - Theme.spacingLg)
        clip: true

        IconButton {
            glyph: EditorState.playing ? Theme.icons.pause : Theme.icons.play
            variant: "text"
            tooltip: EditorState.playing ? qsTr("Pause") : qsTr("Play")
            onClicked: EditorState.togglePlayback()
        }

        Text {
            text: toolbar.panel.formatTime(EditorState.playheadSeconds) + " / " + toolbar.panel.formatTime(EditorState.durationSeconds)
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }

        Rectangle {
            width: Theme.borderWidth
            height: Theme.spacing3xl
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        // CapCut-style exclusive tool modes: Select is the default; cut tools
        // stay sticky until you switch back via Select (or V). Clicking the
        // active cut tool again also returns to Select.
        IconButton {
            glyph: Theme.icons.mousePointer
            variant: "text"
            tooltip: qsTr("Select — normal editing (V)")
            active: toolbar.panel.timelineTool === ""
            onClicked: toolbar.panel.timelineTool = ""
        }
        IconButton {
            glyph: Theme.icons.scissors
            variant: "text"
            tooltip: qsTr("Cut mode — click a clip to split it (B)")
            active: toolbar.panel.timelineTool === "split"
            onClicked: toolbar.panel.timelineTool = toolbar.panel.timelineTool === "split" ? "" : "split"
        }
        IconButton {
            glyph: Theme.icons.chevronsLeft
            variant: "text"
            tooltip: qsTr("Trim start — click a clip to drop everything left of the cut")
            active: toolbar.panel.timelineTool === "trimStart"
            onClicked: toolbar.panel.timelineTool = toolbar.panel.timelineTool === "trimStart" ? "" : "trimStart"
        }
        IconButton {
            glyph: Theme.icons.chevronsRight
            variant: "text"
            tooltip: qsTr("Trim end — click a clip to drop everything right of the cut")
            active: toolbar.panel.timelineTool === "trimEnd"
            onClicked: toolbar.panel.timelineTool = toolbar.panel.timelineTool === "trimEnd" ? "" : "trimEnd"
        }
        IconButton {
            glyph: Theme.icons.audioLines
            variant: "text"
            tooltip: qsTr("Separate audio from video")
            enabled: EditorState.separateAudioAvailable
            onClicked: EditorState.separateAudioFromSelection()
        }
        IconButton {
            glyph: Theme.icons.unlink
            variant: "text"
            tooltip: qsTr("Unlink video and audio")
            enabled: EditorState.unlinkAvailable
            onClicked: EditorState.unlinkSelectedClips()
        }
        IconButton {
            glyph: Theme.icons.linkTwo
            variant: "text"
            tooltip: qsTr("Merge adjacent clips")
            enabled: EditorState.mergeAvailable
            onClicked: EditorState.mergeSelectedClips()
        }
        IconButton { glyph: Theme.icons.copy; variant: "text"; tooltip: qsTr("Copy selection"); onClicked: EditorState.copySelection() }
        IconButton { glyph: Theme.icons.clipboardPaste; variant: "text"; tooltip: qsTr("Paste at current time"); onClicked: EditorState.pasteAtPlayhead() }
        IconButton { glyph: Theme.icons.copyPlus; variant: "text"; tooltip: qsTr("Duplicate clip"); onClicked: EditorState.duplicateSelectedClip() }
        IconButton { glyph: Theme.icons.snowflake; variant: "text"; tooltip: qsTr("Freeze frame at current time"); onClicked: EditorState.freezeFrameAtPlayhead() }
        IconButton { glyph: Theme.icons.trash; variant: "text"; tooltip: qsTr("Delete clip"); onClicked: EditorState.deleteSelectedClip() }

        Rectangle {
            width: Theme.borderWidth
            height: Theme.spacing3xl
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        IconButton {
            glyph: Theme.icons.bookmark
            variant: "text"
            tooltip: qsTr("Add bookmark at current time")
            onClicked: EditorState.addBookmark(EditorState.playheadSeconds, "Mark " + Math.round(EditorState.playheadSeconds))
        }
        IconButton {
            glyph: Theme.icons.undo
            variant: "text"
            tooltip: qsTr("Undo")
            onClicked: EditorState.undo()
            enabled: EditorState.undoAvailable
        }
        IconButton {
            glyph: Theme.icons.redo
            variant: "text"
            tooltip: qsTr("Redo")
            onClicked: EditorState.redo()
            enabled: EditorState.redoAvailable
        }
    }

    Rectangle {
        id: sceneBadge

        // Sits between the two button groups, which are anchored to the
        // toolbar edges and grow freely. Prefer dead centre, but slide
        // aside to stay clear of them, and drop out entirely once the
        // gap can no longer fit the badge.
        readonly property real gapStart: leftControls.x + leftControls.width + 12
        readonly property real gapEnd: rightControls.x - 12

        x: Math.max(gapStart, Math.min((toolbar.width - width) / 2, gapEnd - width))
        anchors.verticalCenter: parent.verticalCenter
        visible: gapEnd - gapStart >= width
        width: sceneRow.implicitWidth + 20
        height: 26
        radius: Theme.radiusSm
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(Theme.panelForeground.r, Theme.panelForeground.g, Theme.panelForeground.b, 0.1)

        Row {
            id: sceneRow
            anchors.centerIn: parent
            spacing: 6

            Text {
                text: qsTr("Scene 1")
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                anchors.verticalCenter: parent.verticalCenter
            }
            IconGlyph {
                glyph: Theme.icons.layers
                iconSize: 14
                iconColor: Theme.mutedForeground
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    Row {
        id: rightControls
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingSm

        IconButton {
            id: magnetButton
            glyph: Theme.icons.magnet
            variant: "text"
            tooltip: qsTr("Toggle snapping")
            active: EditorState.snapEnabled
            onClicked: EditorState.snapEnabled = !EditorState.snapEnabled
        }
        IconButton {
            id: rippleButton
            glyph: Theme.icons.foldHorizontal
            variant: "text"
            tooltip: qsTr("Close gaps when trimming")
            active: EditorState.rippleEnabled
            onClicked: EditorState.rippleEnabled = !EditorState.rippleEnabled
        }

        Rectangle {
            width: Theme.borderWidth
            height: Theme.spacing3xl
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        IconButton {
            glyph: Theme.icons.zoomOut
            variant: "text"
            tooltip: qsTr("Zoom out")
            onClicked: toolbar.panel.zoom = Math.max(toolbar.panel.minZoom, toolbar.panel.zoom / 1.5)
        }
        ThemedSlider {
            id: zoomSlider
            width: 112
            anchors.verticalCenter: parent.verticalCenter
            // Logarithmic mapping so the wide zoom range stays controllable.
            from: 0
            to: 1
            value: Math.log(toolbar.panel.zoom / toolbar.panel.minZoom) / Math.log(toolbar.panel.maxZoom / toolbar.panel.minZoom)
            onMoved: toolbar.panel.zoom = toolbar.panel.minZoom * Math.pow(toolbar.panel.maxZoom / toolbar.panel.minZoom, value)
            // There was no zoom readout anywhere, so the current level
            // was simply unknowable.
            valueFormatter: function () {
                return qsTr("Zoom %1×").arg(toolbar.panel.zoom.toFixed(2))
            }
        }

        // Numeric zoom level, and a click target to return to 1×.
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 44
            text: toolbar.panel.zoom.toFixed(2) + "×"
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeTick
            horizontalAlignment: Text.AlignHCenter

            ThemedToolTip {
                text: qsTr("Zoom level — click to reset to 1×. Ctrl+wheel over the timeline also zooms.")
                visible: zoomLabelMouse.containsMouse
            }

            MouseArea {
                id: zoomLabelMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: toolbar.panel.zoom = 1.0
            }
        }
        IconButton {
            glyph: Theme.icons.zoomIn
            variant: "text"
            tooltip: qsTr("Zoom in")
            onClicked: toolbar.panel.zoom = Math.min(toolbar.panel.maxZoom, toolbar.panel.zoom * 1.5)
        }
    }
}

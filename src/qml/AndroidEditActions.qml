import QtQuick
import Drift
import "components"

// CapCut-style tool strip above the timeline: mode tools and clip actions on the left,
// zoom on the right. Everything here is one tap away because the phone has no toolbar,
// no menu bar and no keyboard to reach any of it another way.
Item {
    id: root

    property var panel: null

    readonly property bool hasSelection: {
        void EditorState.selection
        return EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    }

    readonly property string tool: panel ? panel.timelineTool : ""

    // The window is edge-to-edge and this strip runs the full width, so in landscape
    // the system nav bar sat straight on top of the right-pinned zoom/fit buttons —
    // and fit is the one zoom a pinch cannot stand in for.
    readonly property real leftInset: SafeArea.margins.left
    readonly property real rightInset: SafeArea.margins.right

    function setTool(id) {
        if (!panel)
            return
        panel.timelineTool = panel.timelineTool === id ? "" : id
    }

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

    // Reusable strip button so the row stays uniform and every target is rail-sized.
    component ActionButton: IconButton {
        anchors.verticalCenter: parent.verticalCenter
        buttonSize: Theme.androidIconButtonSize
        iconSize: Theme.iconSizeLg
        variant: "text"
    }

    component Divider: Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.borderWidth
        height: Theme.spacing3xl
        color: Theme.panelBorder
    }

    Flickable {
        anchors.left: parent.left
        anchors.right: zoomRow.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.leftInset
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
            // 2dp put Delete a hair from Separate-audio. The row lives in a
            // horizontal Flickable, so the extra width costs nothing but scroll.
            spacing: Theme.spacingLg

            ActionButton {
                glyph: Theme.icons.mousePointer
                tooltip: qsTr("Select")
                active: root.tool === ""
                onClicked: if (root.panel) root.panel.timelineTool = ""
            }

            ActionButton {
                glyph: Theme.icons.scissors
                tooltip: qsTr("Blade — tap a clip to split")
                active: root.tool === "split"
                onClicked: root.setTool("split")
            }

            ActionButton {
                glyph: Theme.icons.trimStart
                tooltip: qsTr("Trim start — tap a clip to drop everything before the cut")
                active: root.tool === "trimStart"
                onClicked: root.setTool("trimStart")
            }

            ActionButton {
                glyph: Theme.icons.trimEnd
                tooltip: qsTr("Trim end — tap a clip to drop everything after the cut")
                active: root.tool === "trimEnd"
                onClicked: root.setTool("trimEnd")
            }

            Divider { }

            // The clip menu offers cut and copy, so without this the phone's clipboard
            // is write-only: a cut clip could never come back.
            ActionButton {
                glyph: Theme.icons.clipboardPaste
                tooltip: qsTr("Paste at current time")
                onClicked: EditorState.pasteAtPlayhead()
            }

            ActionButton {
                glyph: Theme.icons.copyPlus
                tooltip: qsTr("Duplicate clip")
                enabled: root.hasSelection
                onClicked: EditorState.duplicateSelectedClip()
            }

            ActionButton {
                glyph: Theme.icons.linkTwo
                tooltip: qsTr("Merge adjacent clips")
                enabled: EditorState.mergeAvailable
                onClicked: EditorState.mergeSelectedClips()
            }

            ActionButton {
                glyph: Theme.icons.snowflake
                tooltip: qsTr("Freeze frame at current time")
                onClicked: EditorState.freezeFrameAtPlayhead()
            }

            ActionButton {
                glyph: Theme.icons.audioLines
                tooltip: qsTr("Separate audio from video")
                enabled: EditorState.separateAudioAvailable
                onClicked: EditorState.separateAudioFromSelection()
            }

            ActionButton {
                glyph: Theme.icons.trash
                tooltip: qsTr("Delete")
                enabled: root.hasSelection
                onClicked: EditorState.deleteSelectedClip()
            }

            Divider { }

            // Bookmarks were backend-only on the phone: no way to set one, and no way
            // to reach one that a desktop session had left in the project.
            ActionButton {
                glyph: Theme.icons.bookmark
                tooltip: qsTr("Add or remove a bookmark here")
                onClicked: EditorState.toggleBookmarkAtPlayhead()
            }

            ActionButton {
                glyph: Theme.icons.chevronLeft
                tooltip: qsTr("Previous bookmark")
                enabled: EditorState.bookmarks.length > 0
                onClicked: EditorState.goToPreviousBookmark()
            }

            ActionButton {
                glyph: Theme.icons.chevronsRight
                tooltip: qsTr("Next bookmark")
                enabled: EditorState.bookmarks.length > 0
                onClicked: EditorState.goToNextBookmark()
            }

            Divider { }

            ActionButton {
                glyph: Theme.icons.magnet
                tooltip: qsTr("Toggle snapping")
                active: EditorState.snapEnabled
                onClicked: EditorState.snapEnabled = !EditorState.snapEnabled
            }

            // Beat detection shipped with its only trigger inside the keyframe graph, which
            // the mobile timeline never builds. Analysis covers the whole timeline rather
            // than a clip range because the markers and the snap targets it feeds are
            // timeline-wide here.
            ActionButton {
                glyph: Theme.icons.music
                tooltip: EditorState.beatAnalysisRunning
                         ? qsTr("Analyzing…")
                         : (EditorState.beatGridVisible ? qsTr("Hide beat markers")
                                                        : qsTr("Find the beat and show markers"))
                active: EditorState.beatGridVisible
                enabled: !EditorState.beatAnalysisRunning && EditorState.durationSeconds > 0
                onClicked: {
                    if (EditorState.beatGridVisible) {
                        EditorState.beatGridVisible = false
                    } else {
                        EditorState.beatGridVisible = true
                        EditorState.analyzeBeats(0, EditorState.durationSeconds)
                    }
                }
            }

            ActionButton {
                glyph: Theme.icons.foldHorizontal
                tooltip: qsTr("Close gaps when trimming")
                active: EditorState.rippleEnabled
                onClicked: EditorState.rippleEnabled = !EditorState.rippleEnabled
            }

            // Overlapping two clips is how a transition is created; without this the
            // phone could apply a transition but never make room for one.
            ActionButton {
                glyph: Theme.icons.option
                tooltip: qsTr("Allow clip overlap")
                active: EditorState.allowClipOverlap
                onClicked: EditorState.allowClipOverlap = !EditorState.allowClipOverlap
            }
        }
    }

    Row {
        id: zoomRow
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm + root.rightInset
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingLg

        ActionButton {
            glyph: Theme.icons.zoomOut
            tooltip: qsTr("Zoom out")
            enabled: !!root.panel
            onClicked: if (root.panel) root.panel.setZoom(root.panel.zoom / 1.5)
        }

        ActionButton {
            glyph: Theme.icons.zoomIn
            tooltip: qsTr("Zoom in")
            enabled: !!root.panel
            onClicked: if (root.panel) root.panel.setZoom(root.panel.zoom * 1.5)
        }

        // Replaces the zoom readout: a percentage is not something to act on, and
        // "show me the whole project" is the one zoom request a phone cannot satisfy
        // by pinching — the range is too wide for a single gesture.
        ActionButton {
            glyph: Theme.icons.zoomFit
            tooltip: qsTr("Fit timeline in view")
            enabled: !!root.panel
            onClicked: if (root.panel) root.panel.fitZoom()
        }
    }
}

import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Fixed left-hand track header column: per-track mute/hide/waveform toggles,
// type icon/name, reorder-by-drag handle and the track context menu. Owns the
// track-drag state; the panel only supplies the track list and vertical scroll.
Item {
    id: root

    // Track model (EditorState.tracks) and the timeline's live vertical scroll,
    // so headers stay aligned with their rows.
    property var tracks: []
    property real contentY: 0
    // Desktop uses Theme.trackLabelsWidth; Android passes a narrower value.
    property real labelsWidth: Theme.trackLabelsWidth
    // Phone: grip + mute/hide only (no type glyph / waveform / name band).
    property bool compact: false

    // Track-header reorder: source index and live drop target while dragging.
    property int draggingTrackFrom: -1
    property int draggingTrackTo: -1

    // Pending delete confirmation — index kept until Accept/Reject so the menu
    // can close without wiping the track immediately.
    property int pendingDeleteTrack: -1

    clip: true

    ThemedDialog {
        id: confirmDeleteTrack
        title: qsTr("Delete this track?")
        acceptText: qsTr("Delete track")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        readonly property int clipCount: {
            if (root.pendingDeleteTrack < 0 || root.pendingDeleteTrack >= root.tracks.length)
                return 0
            const clips = root.tracks[root.pendingDeleteTrack].clips
            return clips ? clips.length : 0
        }

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: confirmDeleteTrack.clipCount > 0
                  ? qsTr("This removes the track and its %n clip(s). You can undo afterwards.",
                         "", confirmDeleteTrack.clipCount)
                  : qsTr("This removes the empty track. You can undo afterwards.")
        }

        onAccepted: {
            if (root.pendingDeleteTrack >= 0)
                EditorState.removeTrack(root.pendingDeleteTrack)
            root.pendingDeleteTrack = -1
        }
        onRejected: root.pendingDeleteTrack = -1
    }

    // Must stay in step with TimelinePanel's own height helpers, or the headers
    // drift out of alignment with their rows.
    function trackBaseHeight(type) {
        if (type === "video") return Theme.trackHeightVideo;
        if (type === "audio") return Theme.trackHeightAudio;
        if (type === "shape") return Theme.trackHeightShape;
        if (type === "subtitle") return Theme.trackHeightSubtitle;
        return Theme.trackHeightText;
    }

    function trackHeight(index) {
        if (index < 0 || index >= tracks.length)
            return Theme.trackHeightVideo
        const track = tracks[index]
        const scale = track.heightScale > 0 ? track.heightScale : 1
        return Math.round(Math.max(20, trackBaseHeight(track.type) * scale))
    }

    function trackTypeIcon(type) {
        if (type === "audio") return Theme.icons.music;
        if (type === "text") return Theme.icons.type;
        if (type === "subtitle") return Theme.icons.messageSquare;
        if (type === "shape") return Theme.icons.shapes;
        return Theme.icons.video;
    }

    // Human label for a track type.
    function trackTypeLabel(type) {
        if (type === "audio") return qsTr("Audio");
        if (type === "text") return qsTr("Text");
        if (type === "subtitle") return qsTr("Subtitle");
        if (type === "shape") return qsTr("Shape");
        return qsTr("Video");
    }

    function trackRowTop(index) {
        var cursor = 0
        for (var i = 0; i < index && i < tracks.length; i++)
            cursor += trackHeight(i) + Theme.trackGap
        return cursor
    }

    // Target index for QList::move while dragging a track header.
    function trackMoveTargetAtY(y) {
        if (tracks.length === 0)
            return -1
        var cursor = 0
        for (var i = 0; i < tracks.length; i++) {
            const th = trackHeight(i)
            if (y < cursor + th / 2)
                return i
            cursor += th + Theme.trackGap
        }
        return tracks.length - 1
    }

    function clearTrackDrag() {
        draggingTrackFrom = -1
        draggingTrackTo = -1
    }

    Repeater {
        model: root.tracks.length
        delegate: Item {
            id: trackLabelRow
            // Read through root.tracks (not the EditorState
            // getters) so the toggles rebind on tracksChanged.
            readonly property bool trackMuted: root.tracks[index].muted === true
            readonly property bool trackHidden: root.tracks[index].hidden === true
            readonly property bool trackWaveform: root.tracks[index].showWaveform === true
            width: root.labelsWidth
            height: root.trackHeight(index)
                    + (index < root.tracks.length - 1 ? Theme.trackGap : 0)
            // Follows the timeline's vertical scroll so labels stay
            // aligned with their rows.
            y: root.trackRowTop(index) - root.contentY
            opacity: root.draggingTrackFrom === index ? 0.45 : 1.0

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: root.trackHeight(index)
                color: Theme.panelBorder
            }

            // Drag handle — left-aligned reorder grip.
            IconGlyph {
                anchors.left: parent.left
                anchors.leftMargin: root.compact ? 4 : 8
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: index < root.tracks.length - 1 ? -Theme.trackGap / 2 : 0
                glyph: Theme.icons.gripVertical
                iconSize: root.compact ? 12 : 14
                iconColor: trackDragMouse.containsMouse || root.draggingTrackFrom === index
                           ? Theme.panelForeground : Theme.mutedForeground

                ThemedToolTip {
                    visible: trackDragMouse.containsMouse && root.draggingTrackFrom < 0
                    text: qsTr("Drag to reorder track")
                }

                MouseArea {
                    id: trackDragMouse
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.SizeAllCursor
                    preventStealing: true

                    onPressed: {
                        root.draggingTrackFrom = index
                        root.draggingTrackTo = index
                    }
                    onPositionChanged: (mouse) => {
                        if (root.draggingTrackFrom < 0)
                            return
                        const local = mapToItem(root, mouse.x, mouse.y)
                        root.draggingTrackTo = root.trackMoveTargetAtY(local.y)
                    }
                    onReleased: {
                        if (root.draggingTrackFrom >= 0
                                && root.draggingTrackTo >= 0
                                && root.draggingTrackFrom !== root.draggingTrackTo)
                            EditorState.moveTrack(root.draggingTrackFrom,
                                                  root.draggingTrackTo)
                        root.clearTrackDrag()
                    }
                    onCanceled: root.clearTrackDrag()
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: root.compact ? 4 : 12
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: index < root.tracks.length - 1 ? -Theme.trackGap / 2 : 0
                spacing: root.compact ? 4 : 8

                IconGlyph {
                    visible: root.tracks[index].type === "video"
                             || root.tracks[index].type === "audio"
                    glyph: trackLabelRow.trackMuted ? Theme.icons.volumeOff : Theme.icons.volumeHigh
                    iconSize: 16
                    iconColor: trackLabelRow.trackMuted ? Theme.destructive : Theme.mutedForeground
                    anchors.verticalCenter: parent.verticalCenter

                    ThemedToolTip {
                        visible: muteMouse.containsMouse
                        text: trackLabelRow.trackMuted ? qsTr("Unmute track") : qsTr("Mute track")
                    }

                    MouseArea {
                        id: muteMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: EditorState.setTrackMuted(index, !trackLabelRow.trackMuted)
                    }
                }

                IconGlyph {
                    visible: root.tracks[index].type === "video"
                             || root.tracks[index].type === "text"
                             || root.tracks[index].type === "subtitle"
                             || root.tracks[index].type === "shape"
                    glyph: trackLabelRow.trackHidden ? Theme.icons.eyeOff : Theme.icons.eye
                    iconSize: 16
                    iconColor: trackLabelRow.trackHidden ? Theme.destructive : Theme.mutedForeground
                    anchors.verticalCenter: parent.verticalCenter

                    ThemedToolTip {
                        visible: hideMouse.containsMouse
                        text: trackLabelRow.trackHidden ? qsTr("Show track") : qsTr("Hide track")
                    }

                    MouseArea {
                        id: hideMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: EditorState.setTrackHidden(index, !trackLabelRow.trackHidden)
                    }
                }

                // Toggle the whole track between filmstrip previews and audio waveforms.
                IconGlyph {
                    visible: !root.compact && root.tracks[index].type === "video"
                    glyph: trackLabelRow.trackWaveform ? Theme.icons.audioLines : Theme.icons.film
                    iconSize: 16
                    iconColor: trackLabelRow.trackWaveform ? Theme.primary : Theme.mutedForeground
                    anchors.verticalCenter: parent.verticalCenter

                    ThemedToolTip {
                        visible: waveMouse.containsMouse
                        text: trackLabelRow.trackWaveform ? qsTr("Show thumbnails")
                                                          : qsTr("Show waveform")
                    }

                    MouseArea {
                        id: waveMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: EditorState.setTrackShowWaveform(index, !trackLabelRow.trackWaveform)
                    }
                }

                IconGlyph {
                    visible: !root.compact
                    glyph: root.trackTypeIcon(root.tracks[index].type)
                    iconSize: Theme.iconSizeBase
                    iconColor: Theme.mutedForeground
                    anchors.verticalCenter: parent.verticalCenter

                    // Tracks were identifiable only by this 16px
                    // glyph, with no name and no tooltip.
                    ThemedToolTip {
                        text: root.trackTypeLabel(root.tracks[index].type)
                        visible: typeHover.hovered
                    }

                    HoverHandler { id: typeHover }
                }
            }

            // Track name. Nothing in the header used to say which
            // track this was beyond the type glyph.
            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing3xl + Theme.spacingSm
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingLg
                anchors.top: parent.top
                anchors.topMargin: Theme.spacingMd
                text: root.trackTypeLabel(root.tracks[index].type)
                      + " " + (index + 1)
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTiny
                elide: Text.ElideRight
                visible: !root.compact && root.trackHeight(index) >= 40
            }

            // Track context menu.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: trackContextMenu.popup()

                ThemedContextMenu {
                    id: trackContextMenu

                    ThemedMenuItem {
                        text: trackLabelRow.trackMuted ? qsTr("Unmute track")
                                                       : qsTr("Mute track")
                        icon.name: trackLabelRow.trackMuted ? Theme.icons.volumeHigh
                                                            : Theme.icons.volumeOff
                        onTriggered: EditorState.setTrackMuted(index, !trackLabelRow.trackMuted)
                    }
                    ThemedMenuItem {
                        text: trackLabelRow.trackHidden ? qsTr("Show track")
                                                        : qsTr("Hide track")
                        icon.name: trackLabelRow.trackHidden ? Theme.icons.eye
                                                             : Theme.icons.eyeOff
                        onTriggered: EditorState.setTrackHidden(index, !trackLabelRow.trackHidden)
                    }
                    ThemedMenuItem {
                        visible: root.tracks[index].type === "video"
                        text: trackLabelRow.trackWaveform
                              ? qsTr("Show thumbnails") : qsTr("Show waveform")
                        icon.name: trackLabelRow.trackWaveform
                                   ? Theme.icons.film : Theme.icons.audioLines
                        onTriggered: EditorState.setTrackShowWaveform(
                                         index, !trackLabelRow.trackWaveform)
                    }
                    ThemedMenuSeparator {}
                    ThemedMenuItem {
                        text: qsTr("Reset row height")
                        icon.name: Theme.icons.minimize
                        enabled: root.tracks[index].heightScale !== 1
                        onTriggered: EditorState.setTrackHeightScale(index, 1)
                    }
                    ThemedMenuSeparator {}
                    ThemedMenuItem {
                        text: qsTr("Delete track")
                        icon.name: Theme.icons.trash
                        onTriggered: {
                            root.pendingDeleteTrack = index
                            confirmDeleteTrack.open()
                        }
                    }
                }
            }

            // DAW-style lane zoom: wheel over this header grows/shrinks only
            // this track, so a music track can be made tall for waveform work
            // without zooming the whole timeline. NoButton keeps clicks, the
            // reorder drag and the context menu working underneath.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                z: 40
                onWheel: (wheel) => {
                    // Modified wheel belongs to the timeline's zoom/pan.
                    if (wheel.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) {
                        wheel.accepted = false
                        return
                    }
                    const dy = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y
                                                        : wheel.pixelDelta.y
                    if (dy === 0)
                        return
                    EditorState.nudgeTrackHeightScale(index, dy > 0 ? 1 : -1)
                }
            }
        }
    }

    // Insertion line while reordering tracks.
    Rectangle {
        visible: root.draggingTrackFrom >= 0 && root.draggingTrackTo >= 0
                 && root.draggingTrackFrom !== root.draggingTrackTo
        width: parent.width - 8
        height: 2
        radius: 1
        x: 4
        color: Theme.primary
        z: 10
        y: {
            if (root.draggingTrackTo < 0)
                return 0
            const from = root.draggingTrackFrom
            const to = root.draggingTrackTo
            if (from < to)
                return root.trackRowTop(to) + root.trackHeight(to) - 1
            return root.trackRowTop(to)
        }
    }
}

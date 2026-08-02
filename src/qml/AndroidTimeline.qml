import QtQuick
import QtQuick.Controls.Basic
import Drift
import "components"
import "components/timeline"

// CapCut-style phone timeline: multi-track, seek, select/move/trim/blade, pinch zoom.
// Omits keyframe graph, subtitle lane, marquee, library drops, and transition chrome.
// Edit tools live in AndroidEditActions (above the bottom rail), not on this panel.
Item {
    id: root

    property real zoom: 1.0
    property string timelineTool: ""
    property real cutHoverSeconds: -1
    property int cutHoverTrack: -1
    property int cutHoverClip: -1

    signal openMediaRequested()
    signal openPropertiesRequested()

    function openClipProperties() {
        openPropertiesRequested()
    }

    onTimelineToolChanged: if (timelineTool === "") {
        cutHoverSeconds = -1
        cutHoverTrack = -1
        cutHoverClip = -1
    }

    readonly property real minZoom: 0.05
    readonly property real maxZoom: 40.0
    readonly property real pxPerSecond: Theme.pixelsPerSecondBase * zoom
    readonly property real labelsWidth: Theme.androidTrackLabelsWidth
    readonly property real seekHeaderHeight: Theme.timelineRulerHeight
    // Exposed for clip filmstrip viewport culling (Flickable id is local).
    readonly property real timelineViewX: flick.contentX
    readonly property real timelineViewW: flick.width
    // Bumped when pinch-zoom ends so ClipFilmstrip rebinds Images after the gesture.
    property int filmstripRefreshEpoch: 0

    readonly property real tickStepSeconds: {
        const minLabelPx = 66
        const needed = minLabelPx / pxPerSecond
        const steps = [0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30,
                       60, 120, 300, 600, 900, 1800, 3600]
        for (var i = 0; i < steps.length; i++)
            if (steps[i] >= needed)
                return steps[i]
        return steps[steps.length - 1]
    }

    function formatTick(seconds) {
        const cc = Math.round(Math.max(0, seconds) * 100)
        const pad = (n) => (n < 10 ? "0" : "") + n
        let out = pad(Math.floor(cc / 360000)) + ":"
                + pad(Math.floor((cc % 360000) / 6000)) + ":"
                + pad(Math.floor((cc % 6000) / 100))
        if (tickStepSeconds < 1)
            out += "." + pad(cc % 100)
        return out
    }

    function formatTime(seconds) {
        const total = Math.max(0, Math.round(seconds))
        const m = Math.floor(total / 60)
        const s = total % 60
        return m.toString().padStart(2, "0") + ":" + s.toString().padStart(2, "0")
    }

    readonly property var tracks: EditorState.tracks

    property real snapGuideSeconds: -1
    property int dropTrackIndex: -1
    property real dropStartSeconds: 0
    property real dropDurationSeconds: 0
    property bool dropCreatesNewTrack: false
    property int effectDropTrackIndex: -1
    property int effectDropClipIndex: -1

    property bool moveFollowActive: false
    property int moveLeaderTrack: -1
    property int moveLeaderClip: -1
    property real moveFollowDeltaX: 0

    function beginMoveFollow(trackIndex, clipIndex) {
        moveLeaderTrack = trackIndex
        moveLeaderClip = clipIndex
        moveFollowDeltaX = 0
        moveFollowActive = true
    }
    function updateMoveFollow(deltaX) {
        if (!moveFollowActive)
            return
        moveFollowDeltaX = deltaX
    }
    function clearMoveFollow() {
        moveFollowActive = false
        moveLeaderTrack = -1
        moveLeaderClip = -1
        moveFollowDeltaX = 0
    }

    function showLandingPreview(trackIndex, desiredStart, duration) {
        const snapped = snapClipStart(desiredStart, duration)
        dropTrackIndex = trackIndex
        dropStartSeconds = snapped.start
        dropDurationSeconds = duration
        snapGuideSeconds = snapped.guide
    }
    function clearLandingOutline() {
        dropTrackIndex = -1
        snapGuideSeconds = -1
    }
    function clearLandingPreview() {
        clearLandingOutline()
        dropCreatesNewTrack = false
    }

    function snapClipStart(desiredStart, duration) {
        const l = EditorState.snapTime(desiredStart)
        const rEdge = EditorState.snapTime(desiredStart + duration)
        const lSnapped = Math.abs(l - desiredStart) > 0.0005
        const rSnapped = Math.abs(rEdge - (desiredStart + duration)) > 0.0005
        if (lSnapped && (!rSnapped || Math.abs(l - desiredStart) <= Math.abs(rEdge - duration - desiredStart)))
            return { "start": l, "guide": l }
        if (rSnapped)
            return { "start": rEdge - duration, "guide": rEdge }
        return { "start": desiredStart, "guide": -1 }
    }

    function trackOffsetY(index) {
        var cursor = 0
        for (var i = 0; i < index && i < tracks.length; i++)
            cursor += trackHeight(i) + Theme.trackGap
        return cursor
    }

    function trackBaseHeight(type) {
        if (type === "video") return Theme.trackHeightVideo
        if (type === "audio") return Theme.trackHeightAudio
        if (type === "shape") return Theme.trackHeightShape
        if (type === "subtitle") return Theme.trackHeightSubtitle
        return Theme.trackHeightText
    }

    function trackHeight(index) {
        if (index < 0 || index >= tracks.length)
            return Theme.trackHeightVideo
        const track = tracks[index]
        const scale = track.heightScale > 0 ? track.heightScale : 1
        return Math.round(Math.max(20, trackBaseHeight(track.type) * scale))
    }

    function clipColor(type) {
        if (type === "text") return Theme.clipText
        if (type === "subtitle") return Theme.clipSubtitle
        if (type === "audio") return Theme.clipAudio
        if (type === "graphic") return Theme.clipGraphic
        if (type === "effect") return Theme.clipEffect
        return Theme.clipVideoPlaceholder
    }

    function totalTracksHeight() {
        var h = 0
        for (var i = 0; i < tracks.length; i++) {
            h += trackHeight(i)
            if (i > 0) h += Theme.trackGap
        }
        return h
    }

    function clipIndexAtPosition(trackIndex, xPixels) {
        if (trackIndex < 0 || trackIndex >= tracks.length)
            return -1
        const seconds = xPixels / pxPerSecond
        const clips = tracks[trackIndex].clips
        for (var i = 0; i < clips.length; i++) {
            if (seconds >= clips[i].start && seconds < clips[i].start + clips[i].duration)
                return i
        }
        return -1
    }

    function trackIndexAtY(y) {
        var cursor = 0
        for (var i = 0; i < tracks.length; i++) {
            const th = trackHeight(i)
            if (y >= cursor && y < cursor + th)
                return i
            cursor += th + Theme.trackGap
        }
        return -1
    }

    function zoomAt(contentX, viewportX, factor) {
        const t = contentX / pxPerSecond
        zoom = Math.max(minZoom, Math.min(maxZoom, zoom * factor))
        const newMaxX = Math.max(0, flick.contentWidth - flick.width)
        flick.contentX = Math.max(0, Math.min(newMaxX, t * pxPerSecond - viewportX))
    }

    function ensurePlayheadVisible() {
        const playheadX = EditorState.playheadSeconds * pxPerSecond
        const margin = 64
        var target = -1
        if (playheadX < flick.contentX + margin)
            target = Math.max(0, playheadX - margin)
        else if (playheadX > flick.contentX + flick.width - margin)
            target = Math.min(Math.max(0, flick.contentWidth - flick.width),
                              playheadX - flick.width + margin)
        if (target < 0)
            return
        if (EditorState.playing || flick.dragging || flick.flicking) {
            flick.contentX = target
            return
        }
        scrollToX(target)
    }

    function scrollToX(target) {
        if (flick.dragging || flick.flicking) {
            flick.contentX = target
            return
        }
        contentXAnimation.stop()
        contentXAnimation.from = flick.contentX
        contentXAnimation.to = target
        contentXAnimation.start()
    }

    NumberAnimation {
        id: contentXAnimation
        target: flick
        property: "contentX"
        duration: Theme.durationBase
        easing.type: Theme.easing
    }

    Column {
        anchors.fill: parent

        Row {
            width: parent.width
            height: parent.height

            Column {
                width: root.labelsWidth
                height: parent.height

                Item {
                    width: parent.width
                    height: root.seekHeaderHeight

                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        width: 1
                        height: parent.height
                        color: Theme.panelBorder
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.panelBorder
                    }
                }

                TrackHeaderColumn {
                    width: parent.width
                    height: Math.max(0, parent.height - root.seekHeaderHeight)
                    tracks: root.tracks
                    contentY: flick.contentY
                    labelsWidth: root.labelsWidth
                    compact: true
                }
            }

            Flickable {
                id: flick
                width: parent.width - root.labelsWidth
                height: parent.height
                contentWidth: Math.max(width, (EditorState.durationSeconds + 5) * root.pxPerSecond)
                contentHeight: Math.max(height,
                                        root.seekHeaderHeight + root.totalTracksHeight() + Theme.trackGap)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: AppScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.vertical: AppScrollBar { policy: ScrollBar.AsNeeded }

                PinchHandler {
                    id: pinch
                    target: null
                    property real startZoom: 1
                    property real startContentX: 0
                    property real centroidViewportX: 0

                    onActiveChanged: {
                        if (active) {
                            startZoom = root.zoom
                            startContentX = flick.contentX
                            centroidViewportX = centroid.position.x
                        } else {
                            // Pinch stops dirtying the scene graph; force filmstrip Images
                            // to rebind so Android/ANGLE does not leave blank tiles.
                            root.filmstripRefreshEpoch++
                        }
                    }
                    onScaleChanged: {
                        if (!active)
                            return
                        const factor = scale
                        const next = Math.max(root.minZoom,
                                              Math.min(root.maxZoom, startZoom * factor))
                        if (next === root.zoom)
                            return
                        const t = (startContentX + centroidViewportX)
                                  / (Theme.pixelsPerSecondBase * startZoom)
                        root.zoom = next
                        const newMaxX = Math.max(0, flick.contentWidth - flick.width)
                        flick.contentX = Math.max(0, Math.min(newMaxX,
                                                              t * root.pxPerSecond - centroidViewportX))
                    }
                }

                Item {
                    id: timelineContent
                    width: flick.contentWidth
                    height: flick.contentHeight

                    Item {
                        id: seekStrip
                        width: parent.width
                        y: flick.contentY
                        z: 2
                        height: root.seekHeaderHeight

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.panelBackground
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.panelBorder
                        }

                        MouseArea {
                            id: rulerScrub
                            anchors.fill: parent
                            preventStealing: true
                            z: 1

                            function scrubTo(x) {
                                EditorState.playheadSeconds =
                                    EditorState.snapTime(Math.max(0, x) / root.pxPerSecond)
                            }
                            onPressed: (mouse) => scrubTo(mouse.x)
                            onPositionChanged: (mouse) => {
                                if (pressed)
                                    scrubTo(mouse.x)
                            }
                        }

                        Item {
                            id: ruler
                            width: parent.width
                            height: Theme.timelineRulerHeight
                            z: 0

                            readonly property real tickStepPx: root.tickStepSeconds * root.pxPerSecond
                            readonly property int tickIndexMax: Math.max(0,
                                Math.ceil(flick.contentWidth / Math.max(1, tickStepPx)))
                            readonly property int firstVisibleTick: Math.max(0,
                                Math.floor(flick.contentX / Math.max(1, tickStepPx)) - 1)
                            readonly property int visibleTickCount: Math.min(
                                tickIndexMax - firstVisibleTick + 1,
                                Math.ceil(flick.width / Math.max(1, tickStepPx)) + 3)

                            Repeater {
                                model: Math.max(0, ruler.visibleTickCount)
                                delegate: Item {
                                    readonly property real tickSeconds:
                                        (ruler.firstVisibleTick + index) * root.tickStepSeconds
                                    x: tickSeconds * root.pxPerSecond
                                    width: root.tickStepSeconds * root.pxPerSecond
                                    height: ruler.height

                                    Rectangle {
                                        x: 0
                                        y: parent.height - 10
                                        width: 1
                                        height: 8
                                        color: Qt.rgba(Theme.mutedForeground.r,
                                                       Theme.mutedForeground.g,
                                                       Theme.mutedForeground.b, 0.28)
                                    }
                                    Text {
                                        x: 4
                                        y: 4
                                        text: root.formatTick(parent.tickSeconds)
                                        color: Theme.mutedForeground
                                        font.pixelSize: Theme.fontSizeTick
                                        font.family: Theme.fontFamily
                                    }
                                }
                            }
                        }
                    }

                    EmptyState {
                        x: flick.contentX + (flick.width - width) / 2
                        y: flick.contentY + root.seekHeaderHeight
                           + Math.max(0, (flick.height - root.seekHeaderHeight - height) / 2)
                        width: Math.min(flick.width - Theme.spacing3xl, 320)
                        // AndroidEditor draws a fixed empty-state overlay above the timeline.
                        visible: false
                        glyph: Theme.icons.layers
                        title: qsTr("Your timeline is empty")
                        hint: qsTr("Import media to start editing.")
                        actionText: qsTr("Open Media")
                        actionVariant: "primary"
                        onActionTriggered: root.openMediaRequested()
                    }

                    Column {
                        id: trackColumn
                        y: root.seekHeaderHeight
                        width: parent.width
                        spacing: Theme.trackGap
                        z: 1

                        Repeater {
                            model: root.tracks.length
                            delegate: Rectangle {
                                id: trackRow
                                property int trackIndex: index
                                width: flick.contentWidth
                                height: root.trackHeight(trackIndex)
                                color: Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g,
                                               Theme.panelAccent.b, 0.22)

                                Repeater {
                                    model: root.tracks[trackRow.trackIndex].clips.length
                                    delegate: TimelineClipItem {
                                        panel: root
                                        timelineColumn: trackColumn
                                        trackIndex: trackRow.trackIndex
                                        touchMode: true
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        visible: root.dropTrackIndex >= 0
                        x: root.dropStartSeconds * root.pxPerSecond
                        y: root.seekHeaderHeight + root.trackOffsetY(root.dropTrackIndex)
                        width: root.dropDurationSeconds * root.pxPerSecond
                        height: root.dropTrackIndex >= 0 ? root.trackHeight(root.dropTrackIndex) : 0
                        radius: Theme.radiusSm
                        color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.12)
                        border.width: 2
                        border.color: Theme.primary
                        z: 5
                    }

                    Rectangle {
                        visible: opacity > 0
                        opacity: root.snapGuideSeconds >= 0 ? 1 : 0
                        x: root.snapGuideSeconds * root.pxPerSecond
                        y: root.seekHeaderHeight
                        width: Theme.borderWidth
                        height: root.totalTracksHeight()
                        color: Theme.snapGuide
                        z: 6

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }
                    }

                    Item {
                        id: playhead
                        y: 0
                        z: 3
                        width: Theme.playheadLineWidth
                        height: root.seekHeaderHeight + root.totalTracksHeight()

                        Binding {
                            target: playhead
                            property: "x"
                            value: EditorState.playheadSeconds * root.pxPerSecond
                            when: !playheadDragArea.drag.active && !playheadLineDrag.drag.active
                        }

                        onXChanged: {
                            if (playheadDragArea.drag.active || playheadLineDrag.drag.active)
                                EditorState.playheadSeconds = playhead.x / root.pxPerSecond
                        }

                        function finishSeek() {
                            EditorState.playheadSeconds =
                                EditorState.snapTime(playhead.x / root.pxPerSecond)
                        }

                        Rectangle {
                            anchors.left: parent.left
                            y: Theme.timelineRulerHeight * 0.55
                            width: Theme.playheadLineWidth
                            height: parent.height - y
                            color: Theme.primary
                        }

                        Item {
                            id: playheadHandle
                            width: Theme.playheadHandleSize
                            height: Theme.playheadHandleSize + 2
                            x: -width / 2 + Theme.playheadLineWidth / 2
                            y: 3

                            Rectangle {
                                anchors.top: parent.top
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width
                                height: parent.height * 0.55
                                radius: 2
                                color: Theme.primary
                            }

                            Canvas {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.top
                                anchors.topMargin: parent.height * 0.4
                                width: parent.width
                                height: parent.height * 0.6
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.beginPath()
                                    ctx.moveTo(0, 0)
                                    ctx.lineTo(width, 0)
                                    ctx.lineTo(width * 0.5, height)
                                    ctx.closePath()
                                    ctx.fillStyle = Theme.primary
                                    ctx.fill()
                                }
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                Component.onCompleted: requestPaint()
                            }
                        }

                        MouseArea {
                            id: playheadDragArea
                            width: Math.max(Theme.playheadSeekGrabWidth, Theme.androidIconButtonSize)
                            height: root.seekHeaderHeight
                            x: -(width - Theme.playheadLineWidth) / 2
                            y: 0
                            preventStealing: true
                            drag.target: playhead
                            drag.axis: Drag.XAxis
                            drag.threshold: 0
                            drag.minimumX: 0
                            drag.maximumX: flick.contentWidth - Theme.playheadLineWidth
                            onReleased: playhead.finishSeek()
                        }

                        MouseArea {
                            id: playheadLineDrag
                            width: 12
                            height: parent.height - playheadDragArea.height
                            x: -(width - Theme.playheadLineWidth) / 2
                            y: playheadDragArea.height
                            preventStealing: true
                            drag.target: playhead
                            drag.axis: Drag.XAxis
                            drag.threshold: 0
                            drag.minimumX: 0
                            drag.maximumX: flick.contentWidth - Theme.playheadLineWidth
                            onReleased: playhead.finishSeek()
                        }
                    }

                    Item {
                        id: cutOverlay
                        visible: root.timelineTool !== ""
                        enabled: root.timelineTool !== ""
                        x: 0
                        y: root.seekHeaderHeight
                        width: parent.width
                        height: Math.max(root.totalTracksHeight(), flick.height - root.seekHeaderHeight)
                        z: 20

                        function seconds(mx) {
                            return EditorState.snapTime(Math.max(0, mx) / root.pxPerSecond)
                        }

                        function updateHover(mx, my) {
                            root.cutHoverSeconds = seconds(mx)
                            const trackIdx = root.trackIndexAtY(my)
                            root.cutHoverTrack = trackIdx
                            root.cutHoverClip = trackIdx >= 0
                                ? root.clipIndexAtPosition(trackIdx, Math.max(0, mx))
                                : -1
                        }

                        MouseArea {
                            anchors.fill: parent
                            preventStealing: true
                            acceptedButtons: Qt.LeftButton
                            onPressed: (mouse) => cutOverlay.updateHover(mouse.x, mouse.y)
                            onPositionChanged: (mouse) => cutOverlay.updateHover(mouse.x, mouse.y)
                            onReleased: {
                                root.cutHoverSeconds = -1
                                root.cutHoverTrack = -1
                                root.cutHoverClip = -1
                            }
                            onClicked: (mouse) => {
                                const atSeconds = cutOverlay.seconds(mouse.x)
                                const trackIdx = root.trackIndexAtY(mouse.y)
                                if (trackIdx < 0)
                                    return
                                const clipIdx = root.clipIndexAtPosition(trackIdx, Math.max(0, mouse.x))
                                if (clipIdx < 0)
                                    return
                                EditorState.splitClipAt(trackIdx, clipIdx, atSeconds)
                            }
                        }

                        Column {
                            visible: root.cutHoverSeconds >= 0
                            x: root.cutHoverSeconds * root.pxPerSecond
                            spacing: 3
                            Repeater {
                                model: Math.ceil(cutOverlay.height / 7)
                                delegate: Rectangle {
                                    width: 2
                                    height: 4
                                    color: Theme.destructive
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() {
            if (EditorState.playing)
                root.ensurePlayheadVisible()
        }
    }
}

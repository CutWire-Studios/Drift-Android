import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"
import "components/timeline"

PanelFrame {
    id: root

    property real zoom: 1.0
    property string propertiesTab: ""

    // Timeline tool mode (CapCut-style exclusive modes). One of:
    //   ""          Select — normal editing (toolbar pointer / V)
    //   "split"     Blade — click a clip to split it (toolbar scissors / B)
    //   "trimStart"  click a clip to drop everything left of the cut line
    //   "trimEnd"    click a clip to drop everything right of the cut line
    // While a cut tool is active, hovering the timeline shows a red dashed
    // virtual playhead; trim tools also tint the doomed side of the clip.
    property string timelineTool: ""
    // Snapped position (seconds) of the virtual cut playhead; < 0 when not hovering.
    property real cutHoverSeconds: -1
    // Clip physically under the pointer while a tool is active (-1 when none).
    property int cutHoverTrack: -1
    property int cutHoverClip: -1
    onTimelineToolChanged: if (timelineTool === "") {
        cutHoverSeconds = -1
        cutHoverTrack = -1
        cutHoverClip = -1
    }

    // CapCut: V = Select, B = Blade. Escape while a cut tool is active is
    // handled in Main.qml so it exits the tool without clearing the selection.
    Shortcut {
        sequence: "V"
        context: Qt.ApplicationShortcut
        onActivated: root.timelineTool = ""
    }
    Shortcut {
        sequence: "B"
        context: Qt.ApplicationShortcut
        onActivated: root.timelineTool = "split"
    }

    // Y offset of a track row within the track column (excludes ruler/bookmark).
    function trackOffsetY(index) {
        var cursor = 0
        for (var i = 0; i < index && i < tracks.length; i++)
            cursor += trackHeight(i) + Theme.trackGap
        return cursor
    }
    readonly property real minZoom: 0.05
    readonly property real maxZoom: 40.0
    readonly property real pxPerSecond: Theme.pixelsPerSecondBase * zoom
    // Exposed for clip filmstrip viewport culling (Flickable ids are local).
    readonly property real timelineViewX: flick.contentX
    readonly property real timelineViewW: flick.width
    // Kept for ClipFilmstrip bindings shared with AndroidTimeline (wheel zoom bumps it).
    property int filmstripRefreshEpoch: 0

    // Ruler tick interval (seconds): the smallest "nice" step whose labels still
    // have room to breathe at the current zoom, so timestamps never squash.
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

    // HH:MM:SS, plus .CC hundredths once zoomed into sub-second ticks.
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
    readonly property var tracks: EditorState.tracks
    readonly property real playheadSeconds: EditorState.playheadSeconds
    readonly property int selectedTrack: EditorState.selectedTrack
    readonly property int selectedClip: EditorState.selectedClip

    // Invisible extension above track rows while dragging (only when timeline already has clips).
    readonly property real assetDropTopSlop: EditorState.draggingAssetIndex >= 0 && timelineHasClips()
        ? Theme.newTrackHitSlop : 0

    // Live snap guide (seconds; < 0 when hidden) shown while dragging a clip.
    property real snapGuideSeconds: -1
    // Landing preview outline: where a dragged asset (from the library) or an
    // existing clip being moved would come to rest, snapped.
    property int dropTrackIndex: -1
    property real dropStartSeconds: 0
    property real dropDurationSeconds: 0
    // True while dragging a library asset outside any compatible track row.
    property bool dropCreatesNewTrack: false
    // Clip under an in-progress effect drag (for drop highlight).
    property int effectDropTrackIndex: -1
    property int effectDropClipIndex: -1
    // CapCut-style marquee (drag on empty track space to box-select clips).
    property bool marqueeActive: false
    property bool marqueeAdditive: false
    property real marqueeOriginX: 0
    property real marqueeOriginY: 0
    property real marqueeCurrentX: 0
    property real marqueeCurrentY: 0
    readonly property real marqueeLeft: Math.min(marqueeOriginX, marqueeCurrentX)
    readonly property real marqueeTop: Math.min(marqueeOriginY, marqueeCurrentY)
    readonly property real marqueeWidth: Math.abs(marqueeCurrentX - marqueeOriginX)
    readonly property real marqueeHeight: Math.abs(marqueeCurrentY - marqueeOriginY)

    // Clips whose timeline rect intersects the marquee (track-column coords).
    function clipsInMarquee(left, top, right, bottom) {
        const pairs = []
        for (let t = 0; t < tracks.length; t++) {
            const trackTop = trackOffsetY(t)
            const trackBottom = trackTop + trackHeight(t)
            if (trackBottom < top || trackTop > bottom)
                continue
            const clips = tracks[t].clips
            for (let c = 0; c < clips.length; c++) {
                const clipLeft = clips[c].start * pxPerSecond
                const clipRight = (clips[c].start + clips[c].duration) * pxPerSecond
                if (clipRight < left || clipLeft > right)
                    continue
                pairs.push({ "track": t, "clip": c })
            }
        }
        return pairs
    }

    function applyMarqueeSelection() {
        const hits = clipsInMarquee(marqueeLeft, marqueeTop,
                                    marqueeLeft + marqueeWidth,
                                    marqueeTop + marqueeHeight)
        if (marqueeAdditive) {
            const merged = []
            const seen = {}
            const existing = EditorState.selection
            for (let i = 0; i < existing.length; i++) {
                const key = existing[i].track + ":" + existing[i].clip
                seen[key] = true
                merged.push({ "track": existing[i].track, "clip": existing[i].clip })
            }
            for (let i = 0; i < hits.length; i++) {
                const key = hits[i].track + ":" + hits[i].clip
                if (seen[key])
                    continue
                seen[key] = true
                merged.push(hits[i])
            }
            EditorState.setSelection(merged)
        } else {
            EditorState.setSelection(hits)
        }
    }

    function endMarquee(apply) {
        if (!marqueeActive)
            return
        if (apply && (marqueeWidth > 2 || marqueeHeight > 2))
            applyMarqueeSelection()
        else if (apply && !marqueeAdditive)
            EditorState.clearSelection()
        marqueeActive = false
    }

    // While dragging a clip, other selected clips (linked A/V partners included)
    // ride along on the X axis so they don't sit still until drop. CapCut-style.
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

    // Shared by library drops and in-timeline clip moves so both snap and show
    // the same outline the same way.
    function showLandingPreview(trackIndex, desiredStart, duration) {
        const snapped = snapClipStart(desiredStart, duration)
        dropTrackIndex = trackIndex
        dropStartSeconds = snapped.start
        dropDurationSeconds = duration
        snapGuideSeconds = snapped.guide
    }

    // Drops only the landing outline. dropCreatesNewTrack belongs to the
    // panel-level drop area: a track row leaving the drag must not clear it,
    // since reaching the new-track slop always fires a leave on row 0.
    function clearLandingOutline() {
        dropTrackIndex = -1
        snapGuideSeconds = -1
    }

    function clearLandingPreview() {
        clearLandingOutline()
        dropCreatesNewTrack = false
    }

    function clearEffectDropHighlight() {
        effectDropTrackIndex = -1
        effectDropClipIndex = -1
    }

    function updateEffectDropHighlight(trackIndex, xPixels) {
        const clipIndex = clipIndexAtPosition(trackIndex, xPixels)
        effectDropTrackIndex = clipIndex >= 0 ? trackIndex : -1
        effectDropClipIndex = clipIndex
    }

    // Find the outgoing (earlier) clip index for a transition drop at timeline x.
    function transitionLeftClipAtPosition(trackIndex, xPixels) {
        if (trackIndex < 0 || trackIndex >= tracks.length)
            return -1
        const track = tracks[trackIndex]
        if (track.type !== "video" && track.type !== "shape")
            return -1
        const seconds = xPixels / pxPerSecond
        const clips = track.clips
        let best = -1
        let bestDist = 1e9
        for (let i = 0; i < clips.length; i++) {
            const left = clips[i]
            for (let j = 0; j < clips.length; j++) {
                if (i === j)
                    continue
                const right = clips[j]
                if (right.start < left.start)
                    continue
                const leftEnd = left.start + left.duration
                const gap = right.start - leftEnd
                if (gap > 0.001)
                    continue
                let regionStart
                let regionEnd
                if (right.start < leftEnd) {
                    regionStart = right.start
                    regionEnd = leftEnd
                } else {
                    regionStart = leftEnd - 0.25
                    regionEnd = leftEnd + 0.25
                }
                if (seconds >= regionStart && seconds <= regionEnd) {
                    const mid = (regionStart + regionEnd) / 2
                    const dist = Math.abs(seconds - mid)
                    if (dist < bestDist) {
                        bestDist = dist
                        best = i
                    }
                }
            }
        }
        return best
    }

    function applyTransitionDrop(trackIndex, xPixels, kind) {
        const leftClip = transitionLeftClipAtPosition(trackIndex, xPixels)
        if (leftClip < 0 || !kind || kind.length === 0)
            return
        EditorState.addTransition(trackIndex, leftClip, kind, 0.5)
    }

    function assetDurationSeconds(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        if (!asset)
            return 5.0
        if (asset.kind === "image" || !(asset.durationSeconds > 0))
            return 5.0
        return asset.durationSeconds
    }

    // Snap a clip's desired start against timeline targets, testing both edges.
    // Returns {start, guide}; guide < 0 means no snap occurred.
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

    // Default row height for a track type, before the per-track scale.
    function trackBaseHeight(type) {
        if (type === "video") return Theme.trackHeightVideo;
        if (type === "audio") return Theme.trackHeightAudio;
        if (type === "shape") return Theme.trackHeightShape;
        if (type === "subtitle") return Theme.trackHeightSubtitle;
        return Theme.trackHeightText;
    }

    // Actual row height, including the lane's DAW-style vertical zoom.
    function trackHeight(index) {
        if (index < 0 || index >= tracks.length)
            return Theme.trackHeightVideo
        const track = tracks[index]
        const scale = track.heightScale > 0 ? track.heightScale : 1
        return Math.round(Math.max(20, trackBaseHeight(track.type) * scale))
    }

    function clipColor(type) {
        if (type === "text") return Theme.clipText;
        if (type === "subtitle") return Theme.clipSubtitle;
        if (type === "audio") return Theme.clipAudio;
        if (type === "graphic") return Theme.clipGraphic;
        if (type === "effect") return Theme.clipEffect;
        return Theme.clipVideoPlaceholder; // video: no flat fill, thumbnails would go here
    }

    function totalTracksHeight() {
        var h = 0;
        for (var i = 0; i < tracks.length; i++) {
            h += trackHeight(i);
            if (i > 0) h += Theme.trackGap;
        }
        return h;
    }

    // Finds the clip (if any) under a given x position (px) on a track, for
    // dropping an effect card directly onto a clip.
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

    function timelineHasClips() {
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].clips.length > 0)
                return true
        }
        return false
    }

    function firstCompatibleTrackIndex(assetIndex) {
        for (var i = 0; i < tracks.length; i++) {
            if (EditorState.trackAcceptsAsset(i, assetIndex))
                return i
        }
        return -1
    }

    function trackIndexAtY(y) {
        var cursor = 0;
        for (var i = 0; i < tracks.length; i++) {
            const th = trackHeight(i);
            if (y >= cursor && y < cursor + th)
                return i;
            cursor += th + Theme.trackGap;
        }
        return -1;
    }

    function updateAssetDropPreview(assetIndex, dropX, dropY) {
        const duration = assetDurationSeconds(assetIndex)
        const desired = Math.max(0, dropX / pxPerSecond)

        if (!timelineHasClips()) {
            const trackIdx = firstCompatibleTrackIndex(assetIndex)
            if (trackIdx >= 0) {
                dropCreatesNewTrack = false
                showLandingPreview(trackIdx, desired, duration)
            } else {
                clearLandingPreview()
            }
            return
        }

        if (assetDropTopSlop > 0 && dropY < assetDropTopSlop) {
            const snapped = snapClipStart(desired, duration)
            dropCreatesNewTrack = true
            dropTrackIndex = -1
            dropStartSeconds = snapped.start
            dropDurationSeconds = duration
            snapGuideSeconds = snapped.guide
            return
        }

        const trackIdx = trackIndexAtY(dropY - assetDropTopSlop)
        if (trackIdx >= 0 && EditorState.trackAcceptsAsset(trackIdx, assetIndex)) {
            dropCreatesNewTrack = false
            showLandingPreview(trackIdx, desired, duration)
        } else {
            const snapped = snapClipStart(desired, duration)
            dropCreatesNewTrack = true
            dropTrackIndex = -1
            dropStartSeconds = snapped.start
            dropDurationSeconds = duration
            snapGuideSeconds = snapped.guide
        }
    }

    function performAssetDrop(assetIndex, dropX, dropY) {
        const atSeconds = Math.max(0, dropX / pxPerSecond)
        if (assetIndex < 0)
            return

        function runAdd() {
            if (!timelineHasClips()) {
                const trackIdx = firstCompatibleTrackIndex(assetIndex)
                if (trackIdx >= 0)
                    EditorState.addClipFromAssetAt(assetIndex, trackIdx, atSeconds)
                return
            }

            if (assetDropTopSlop > 0 && dropY < assetDropTopSlop) {
                EditorState.addClipFromAssetOnNewTrack(assetIndex, atSeconds)
                return
            }

            const trackIdx = trackIndexAtY(dropY - assetDropTopSlop)
            if (trackIdx >= 0 && EditorState.trackAcceptsAsset(trackIdx, assetIndex))
                EditorState.addClipFromAssetAt(assetIndex, trackIdx, atSeconds)
            else
                EditorState.addClipFromAssetOnNewTrack(assetIndex, atSeconds)
        }

        if (typeof Window !== "undefined" && Window.window && Window.window.configureAndAddAsset)
            Window.window.configureAndAddAsset(assetIndex, runAdd)
        else
            runAdd()
    }

    function handleTimelineWheel(wheel) {
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        if (wheel.modifiers & Qt.ControlModifier) {
            const t = wheel.x / pxPerSecond
            const viewportX = wheel.x - flick.contentX
            const factor = wheel.angleDelta.y > 0 ? 1.15 : 1.0 / 1.15
            zoom = Math.max(minZoom, Math.min(maxZoom, zoom * factor))
            const newMaxX = Math.max(0, flick.contentWidth - flick.width)
            flick.contentX = Math.max(0, Math.min(newMaxX, t * pxPerSecond - viewportX))
            return
        }

        const dy = wheel.angleDelta.y
        const dx = wheel.angleDelta.x

        // Shift forces horizontal scrolling regardless of overflow.
        if (wheel.modifiers & Qt.ShiftModifier) {
            const delta = dy !== 0 ? dy : dx
            flick.contentX = Math.max(0, Math.min(maxX, flick.contentX - delta))
            return
        }

        // Trackpad horizontal component always scrolls horizontally.
        if (dx !== 0)
            flick.contentX = Math.max(0, Math.min(maxX, flick.contentX - dx))

        // Vertical wheel scrolls the tracks when they overflow the viewport;
        // otherwise it falls back to horizontal so short timelines keep the
        // previous wheel-to-pan behaviour.
        if (dy !== 0) {
            if (maxY > 0)
                flick.contentY = Math.max(0, Math.min(maxY, flick.contentY - dy))
            else
                flick.contentX = Math.max(0, Math.min(maxX, flick.contentX - dy))
        }
    }

    function formatTime(seconds) {
        const total = Math.max(0, Math.round(seconds));
        const m = Math.floor(total / 60);
        const s = total % 60;
        return m.toString().padStart(2, "0") + ":" + s.toString().padStart(2, "0");
    }

    function ensurePlayheadVisible() {
        const playheadX = EditorState.playheadSeconds * pxPerSecond;
        const margin = 64;
        var target = -1
        if (playheadX < flick.contentX + margin)
            target = Math.max(0, playheadX - margin)
        else if (playheadX > flick.contentX + flick.width - margin)
            target = Math.min(Math.max(0, flick.contentWidth - flick.width),
                              playheadX - flick.width + margin)
        if (target < 0)
            return
        // During play, assign contentX directly — restarting a NumberAnimation
        // every ~16ms on a multi-hour Flickable freezes the UI.
        if (EditorState.playing || flick.dragging || flick.flicking) {
            flick.contentX = target
            return
        }
        scrollToX(target)
    }

    // Smooth horizontal scroll helper. Skipped while the user is dragging the
    // view, so it never fights a flick in progress.
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

        // === toolbar =================================================================
        TimelineToolbar {
            id: toolbar
            width: parent.width
            panel: root
        }

        // === ruler + track labels + tracks ================================================
        Column {
            width: parent.width
            height: parent.height - toolbar.height

            KeyframeGraph {
                id: keyframesBar
                width: parent.width
                pxPerSecond: root.pxPerSecond
                labelsWidth: Theme.trackLabelsWidth
                propertiesTab: root.propertiesTab
                // Keep keys/playhead lined up with the track scroll view below.
                contentX: flick.contentX
                contentWidth: flick.contentWidth
            }

            SubtitleCueLane {
                id: subtitleLane
                width: parent.width
                pxPerSecond: root.pxPerSecond
                labelsWidth: Theme.trackLabelsWidth
                contentX: flick.contentX
                contentWidth: flick.contentWidth
            }

            Row {
                width: parent.width
                height: parent.height - keyframesBar.height - subtitleLane.height

            // --- fixed left label column --------------------------------------------
            Column {
                width: Theme.trackLabelsWidth
                height: parent.height

                // CapCut-style: add-track sits at the timeline origin, above the
                // track headers, not buried in the top toolbar.
                Item {
                    width: parent.width
                    height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight

                    IconButton {
                        id: addTrackButton
                        anchors.centerIn: parent
                        glyph: Theme.icons.plus
                        variant: "text"
                        tooltip: qsTr("Add new track")
                        onClicked: addTrackMenu.open()

                        NewTrackMenu {
                            id: addTrackMenu
                            x: Math.max(0, (addTrackButton.width - width) / 2)
                            y: addTrackButton.height + 4
                        }
                    }

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
                    id: trackLabelsArea
                    width: parent.width
                    // Clamped: went negative at small panel heights, which spilled
                    // the absolutely-positioned label rows out of the column.
                    height: Math.max(0, parent.height - Theme.timelineRulerHeight
                                        - Theme.timelineBookmarkRowHeight)
                    tracks: root.tracks
                    contentY: flick.contentY
                }
            }

            // --- scrollable ruler + tracks --------------------------------------------
            Flickable {
                id: flick
                width: parent.width - Theme.trackLabelsWidth
                height: parent.height

                // Height of the pinned ruler + bookmark strip at the top.
                readonly property real headerHeight: Theme.timelineRulerHeight
                                                     + Theme.timelineBookmarkRowHeight

                contentWidth: Math.max(width, (EditorState.durationSeconds + 5) * root.pxPerSecond)
                // Was `height`, so once the tracks were taller than the panel the
                // lower ones were silently truncated and could not be reached at
                // all. totalTracksHeight() was already computed but never used.
                contentHeight: Math.max(height,
                                        headerHeight + root.totalTracksHeight() + Theme.trackGap)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: AppScrollBar { policy: ScrollBar.AlwaysOn }
                ScrollBar.vertical: AppScrollBar { }

                Item {
                    id: timelineContent
                    width: flick.contentWidth
                    height: flick.contentHeight

                    // Wheel handling: scoped to the ruler so it does not block timeline drops.
                    MouseArea {
                        id: rulerWheelArea
                        width: parent.width
                        y: flick.contentY
                        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        z: 1
                        acceptedButtons: Qt.NoButton
                        onWheel: (wheel) => root.handleTimelineWheel(wheel)
                    }

                    // Library asset drops
                    DropArea {
                        id: timelineAssetDrop
                        enabled: EditorState.draggingAssetIndex >= 0
                        opacity: 0
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight - root.assetDropTopSlop
                        width: parent.width
                        height: Math.max(root.totalTracksHeight() + root.assetDropTopSlop,
                                         Theme.trackHeightVideo)
                        z: 250
                        keys: ["text/plain"]

                        function assetIndexFromDrop(drop) {
                            if (EditorState.draggingAssetIndex >= 0)
                                return EditorState.draggingAssetIndex
                            const text = drop.hasText ? drop.text : drop.getDataAsString("text/plain")
                            const idx = parseInt(text)
                            return isNaN(idx) ? -1 : idx
                        }

                        function updateAssetDrag(drop) {
                            const assetIndex = assetIndexFromDrop(drop)
                            if (assetIndex < 0) {
                                root.clearLandingPreview()
                                return
                            }
                            root.updateAssetDropPreview(assetIndex, drop.x, drop.y)
                        }

                        onEntered: (drop) => updateAssetDrag(drop)
                        onPositionChanged: (drop) => updateAssetDrag(drop)
                        onExited: root.clearLandingPreview()
                        onDropped: (drop) => {
                            drop.accept(Qt.CopyAction)
                            const assetIndex = assetIndexFromDrop(drop)
                            root.clearLandingPreview()
                            root.performAssetDrop(assetIndex, drop.x, drop.y)
                        }
                    }

                    // Horizontal scroll / zoom wheel over track rows (below the drop overlay).
                    MouseArea {
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight - root.assetDropTopSlop
                        width: parent.width
                        height: Math.max(root.totalTracksHeight() + root.assetDropTopSlop,
                                         Theme.trackHeightVideo)
                        z: 150
                        acceptedButtons: Qt.NoButton
                        onWheel: (wheel) => root.handleTimelineWheel(wheel)
                    }

                    // seek strip (ruler + bookmark lane) ------------------------------------
                    // CapCut/Premiere-style: the whole pinned header is one scrub area so
                    // the playhead is easy to move without hunting for a tiny hit target.
                    Item {
                        id: seekStrip
                        width: parent.width
                        y: flick.contentY
                        z: 2
                        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.panelBackground
                        }

                        // Subtle bottom edge so the seek strip reads as a distinct bar.
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
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            preventStealing: true
                            // Leave room for the playhead head so its drag can start first.
                            z: 1

                            function scrubTo(x) {
                                EditorState.playheadSeconds =
                                    EditorState.snapTime(Math.max(0, x) / root.pxPerSecond)
                            }

                            // Seeking is not a selection change: scrubbing to look at a
                            // different point used to drop the clip you were editing.
                            onPressed: (mouse) => scrubTo(mouse.x)
                            onPositionChanged: (mouse) => {
                                if (pressed)
                                    scrubTo(mouse.x)
                            }

                            ThemedToolTip {
                                text: qsTr("Click or drag to seek")
                                visible: rulerScrub.containsMouse && !rulerScrub.pressed
                                         && !playheadDragArea.drag.active
                            }
                        }

                        // Time ticks live in the upper half of the seek strip.
                        // Only instantiate ticks that intersect the viewport — a 2h
                        // timeline at 1× zoom would otherwise create thousands of Items.
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

                        Item {
                            id: bookmarkRow
                            y: Theme.timelineRulerHeight
                            width: parent.width
                            height: Theme.timelineBookmarkRowHeight
                            z: 2

                            Repeater {
                                model: EditorState.bookmarks
                                delegate: Rectangle {
                                    x: modelData.seconds * root.pxPerSecond - width / 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: Theme.primary
                                    z: 3

                                    ThemedToolTip {
                                        visible: bookmarkMouse.containsMouse
                                        text: modelData.label + " @ "
                                              + root.formatTime(modelData.seconds)
                                    }

                                    MouseArea {
                                        id: bookmarkMouse
                                        anchors.fill: parent
                                        anchors.margins: -6
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        // Stop the seek strip from eating bookmark clicks.
                                        preventStealing: true
                                        onClicked: EditorState.goToBookmark(index)
                                    }
                                }
                            }
                        }
                    }

                    // A project with no tracks used to render as a completely
                    // blank timeline, with the only route to a first track being
                    // one button among sixteen in the toolbar.
                    EmptyState {
                        x: flick.contentX + (flick.width - width) / 2
                        y: flick.contentY + flick.headerHeight
                           + Math.max(0, (flick.height - flick.headerHeight - height) / 2)
                        width: Math.min(flick.width - Theme.spacing3xl, 320)
                        visible: root.tracks.length === 0
                        z: 4
                        glyph: Theme.icons.layers
                        title: qsTr("Your timeline is empty")
                        hint: qsTr("Drag media here from the library, or add an empty track to start.")
                        actionText: qsTr("New track")
                        actionVariant: "primary"
                        onActionTriggered: newTrackMenuFromEmpty.open()

                        NewTrackMenu {
                            id: newTrackMenuFromEmpty
                            y: parent.height
                        }
                    }

                    // track rows ---------------------------------------------------------
                    // CapCut marquee: drag empty track space to box-select.
                    // Sits under the track column so clips still receive presses;
                    // empty gaps fall through to this grabber.
                    MouseArea {
                        id: marqueeArea
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        height: Math.max(root.totalTracksHeight() + Theme.trackGap,
                                         flick.height - flick.headerHeight)
                        z: 0
                        enabled: root.timelineTool === ""
                        acceptedButtons: Qt.LeftButton
                        // CapCut: empty-space drag is marquee, not flick-scroll.
                        preventStealing: true
                        cursorShape: pressed ? Qt.CrossCursor : Qt.ArrowCursor

                        onPressed: (mouse) => {
                            root.marqueeAdditive = (mouse.modifiers & Qt.ShiftModifier) !== 0
                            root.marqueeOriginX = mouse.x
                            root.marqueeOriginY = mouse.y
                            root.marqueeCurrentX = mouse.x
                            root.marqueeCurrentY = mouse.y
                            root.marqueeActive = true
                            if (!root.marqueeAdditive)
                                EditorState.clearSelection()
                        }
                        onPositionChanged: (mouse) => {
                            if (!root.marqueeActive)
                                return
                            root.marqueeCurrentX = Math.max(0, mouse.x)
                            root.marqueeCurrentY = Math.max(0, mouse.y)
                            if (root.marqueeWidth > 2 || root.marqueeHeight > 2)
                                root.applyMarqueeSelection()
                        }
                        onReleased: root.endMarquee(true)
                        onCanceled: root.endMarquee(false)
                    }

                    Column {
                        id: trackColumn
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        spacing: Theme.trackGap
                        z: 1

                        // Landing preview when creating a new track above existing rows.
                        Rectangle {
                            visible: root.dropCreatesNewTrack
                            x: root.dropStartSeconds * root.pxPerSecond
                            width: root.dropDurationSeconds * root.pxPerSecond
                            height: root.trackBaseHeight(EditorState.trackTypeForAsset(EditorState.draggingAssetIndex))
                            radius: Theme.radiusSm
                            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.12)
                            border.width: 2
                            border.color: Theme.primary
                            z: 5
                        }

                        Repeater {
                            model: root.tracks.length
                            delegate: Rectangle {
                                id: trackRow
                                property int trackIndex: index
                                width: flick.contentWidth
                                height: root.trackHeight(trackIndex)
                                // Faint row tint on hover, and an empty track now
                                // reads as a track rather than as blank space.
                                color: trackHover.hovered
                                       ? Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g,
                                                 Theme.panelAccent.b, 0.5)
                                       : Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g,
                                                 Theme.panelAccent.b, 0.22)

                                Behavior on color {
                                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }

                                HoverHandler { id: trackHover }

                                DropArea {
                                    anchors.fill: parent
                                    keys: ["text/plain", "application/x-drift-effect",
                                           "application/x-drift-audio-effect",
                                           "application/x-drift-shape", "application/x-drift-transition"]

                                    function isEffectDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-effect") !== -1
                                    }

                                    function isAudioEffectDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-audio-effect") !== -1
                                    }

                                    function isShapeDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-shape") !== -1
                                    }

                                    function isTransitionDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-transition") !== -1
                                    }

                                    function assetIndexFromDrop(drop) {
                                        if (EditorState.draggingAssetIndex >= 0)
                                            return EditorState.draggingAssetIndex
                                        const text = drop.hasText ? drop.text : drop.getDataAsString("text/plain")
                                        const idx = parseInt(text)
                                        return isNaN(idx) ? -1 : idx
                                    }

                                    function updateAssetPreview(drop) {
                                        if (isTransitionDrag(drop)) {
                                            root.clearLandingPreview()
                                            root.clearEffectDropHighlight()
                                            return
                                        }
                                        if (isEffectDrag(drop) || isAudioEffectDrag(drop)) {
                                            root.updateEffectDropHighlight(trackRow.trackIndex, drop.x)
                                            return
                                        }
                                        root.clearEffectDropHighlight()
                                        if (isShapeDrag(drop)) {
                                            if (root.tracks[trackRow.trackIndex].type === "shape") {
                                                const desired = Math.max(0, drop.x / root.pxPerSecond)
                                                root.showLandingPreview(trackRow.trackIndex, desired, 5.0)
                                            } else {
                                                root.clearLandingPreview()
                                            }
                                            return
                                        }
                                        const assetIndex = assetIndexFromDrop(drop)
                                        if (assetIndex < 0)
                                            return
                                        const columnPos = trackRow.mapFromItem(timelineAssetDrop, drop.x, drop.y)
                                        if (!EditorState.trackAcceptsAsset(trackRow.trackIndex, assetIndex))
                                            return
                                        if (!root.timelineHasClips())
                                            return
                                        if (root.assetDropTopSlop > 0 && columnPos.y < root.assetDropTopSlop)
                                            return
                                        if (root.trackIndexAtY(columnPos.y - root.assetDropTopSlop) !== trackRow.trackIndex)
                                            return
                                        const duration = root.assetDurationSeconds(assetIndex)
                                        const desired = Math.max(0, drop.x / root.pxPerSecond)
                                        root.showLandingPreview(trackRow.trackIndex, desired, duration)
                                    }

                                    onEntered: (drop) => updateAssetPreview(drop)
                                    onPositionChanged: (drop) => updateAssetPreview(drop)
                                    onExited: {
                                        root.clearLandingOutline()
                                        root.clearEffectDropHighlight()
                                    }
                                    onDropped: (drop) => {
                                        drop.accept(Qt.CopyAction)
                                        if (isTransitionDrag(drop)) {
                                            const kind = drop.getDataAsString("application/x-drift-transition")
                                            root.applyTransitionDrop(trackRow.trackIndex, drop.x, kind)
                                            return
                                        }
                                        if (isEffectDrag(drop)) {
                                            const effectId = drop.getDataAsString("application/x-drift-effect")
                                            const clipIndex = root.clipIndexAtPosition(trackRow.trackIndex, drop.x)
                                            root.clearEffectDropHighlight()
                                            if (clipIndex >= 0 && effectId.length > 0) {
                                                EditorState.addEffect(trackRow.trackIndex, clipIndex, effectId)
                                                EditorState.selectClip(trackRow.trackIndex, clipIndex)
                                            }
                                            return
                                        }
                                        if (isAudioEffectDrag(drop)) {
                                            const effectId = drop.getDataAsString("application/x-drift-audio-effect")
                                            const clipIndex = root.clipIndexAtPosition(trackRow.trackIndex, drop.x)
                                            root.clearEffectDropHighlight()
                                            if (clipIndex >= 0 && effectId.length > 0) {
                                                EditorState.addAudioEffect(trackRow.trackIndex, clipIndex, effectId)
                                                EditorState.selectClip(trackRow.trackIndex, clipIndex)
                                            }
                                            return
                                        }
                                        if (isShapeDrag(drop)) {
                                            const shapeId = drop.getDataAsString("application/x-drift-shape")
                                            const atSeconds = Math.max(0, drop.x / root.pxPerSecond)
                                            root.clearLandingPreview()
                                            EditorState.addShapeClipAt(shapeId, trackRow.trackIndex, atSeconds)
                                            return
                                        }
                                        const assetIndex = assetIndexFromDrop(drop)
                                        if (assetIndex < 0)
                                            return
                                        const columnPos = trackRow.mapFromItem(timelineAssetDrop, drop.x, drop.y)
                                        root.clearLandingPreview()
                                        root.performAssetDrop(assetIndex, drop.x, columnPos.y)
                                    }
                                }

                                Rectangle {
                                    visible: root.dropCreatesNewTrack && trackRow.trackIndex === 0
                                    anchors.top: parent.top
                                    width: parent.width
                                    height: 3
                                    color: Theme.primary
                                    z: 6
                                }

                                // Landing preview outline (library drop or clip move).
                                Rectangle {
                                    visible: root.dropTrackIndex === trackRow.trackIndex
                                    x: root.dropStartSeconds * root.pxPerSecond
                                    width: root.dropDurationSeconds * root.pxPerSecond
                                    height: parent.height
                                    radius: Theme.radiusSm
                                    color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.12)
                                    border.width: 2
                                    border.color: Theme.primary
                                    z: 5
                                }

                                Repeater {
                                    model: root.tracks[trackRow.trackIndex].clips.length
                                    delegate: TimelineClipItem {
                                        // panel: root is safe — TimelineClipItem's id is clipItem,
                                        // so it does not shadow TimelinePanel's root.
                                        panel: root
                                        timelineColumn: trackColumn
                                        trackIndex: trackRow.trackIndex
                                    }
                                }

                                // Transition overlap regions (purple) — above clips for hit-testing.
                                Repeater {
                                    model: Math.max(0, root.tracks[trackRow.trackIndex].clips.length)
                                    delegate: Item {
                                        id: transitionRegion
                                        property int leftClipIndex: modelData
                                        property var leftClip: root.tracks[trackRow.trackIndex].clips[leftClipIndex]
                                        property string trackType: root.tracks[trackRow.trackIndex].type
                                        property var transitionData: EditorState.transitionBetweenClips(
                                                                         trackRow.trackIndex, leftClipIndex)
                                        property bool hasTransition: transitionData
                                                                       && Object.keys(transitionData).length > 0
                                        property bool transitionSelected: EditorState.selectedTransitionTrack === trackRow.trackIndex
                                                                          && EditorState.selectedTransitionLeftClip === leftClipIndex

                                        // Find partner clip that abuts/overlaps this one.
                                        property int partnerIndex: {
                                            const clips = root.tracks[trackRow.trackIndex].clips
                                            const left = leftClip
                                            if (!left)
                                                return -1
                                            let best = -1
                                            let bestStart = 1e12
                                            for (let i = 0; i < clips.length; i++) {
                                                if (i === leftClipIndex)
                                                    continue
                                                const right = clips[i]
                                                if (right.start < left.start)
                                                    continue
                                                const gap = right.start - (left.start + left.duration)
                                                if (gap > 0.001)
                                                    continue
                                                if (right.start < bestStart) {
                                                    bestStart = right.start
                                                    best = i
                                                }
                                            }
                                            return best
                                        }
                                        property var rightClip: partnerIndex >= 0
                                            ? root.tracks[trackRow.trackIndex].clips[partnerIndex]
                                            : null
                                        property bool clipsLinked: partnerIndex >= 0
                                        property real regionStart: {
                                            if (!hasTransition && !clipsLinked)
                                                return 0
                                            if (hasTransition && transitionData.start !== undefined)
                                                return transitionData.start
                                            if (!rightClip)
                                                return 0
                                            const leftEnd = leftClip.start + leftClip.duration
                                            if (rightClip.start < leftEnd)
                                                return rightClip.start
                                            return leftEnd - 0.25
                                        }
                                        property real regionEnd: {
                                            if (!hasTransition && !clipsLinked)
                                                return 0
                                            if (hasTransition && transitionData.end !== undefined)
                                                return transitionData.end
                                            if (!rightClip)
                                                return 0
                                            const leftEnd = leftClip.start + leftClip.duration
                                            if (rightClip.start < leftEnd)
                                                return leftEnd
                                            return leftEnd + 0.25
                                        }
                                        property bool showRegion: (trackType === "video" || trackType === "shape")
                                                                  && clipsLinked
                                                                  && regionEnd > regionStart

                                        z: 10
                                        visible: showRegion
                                        x: regionStart * root.pxPerSecond
                                        width: Math.max(8, (regionEnd - regionStart) * root.pxPerSecond)
                                        height: parent.height - Theme.clipSelectionRingWidth * 2
                                        y: Theme.clipSelectionRingWidth

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: Theme.radiusSm
                                            color: transitionRegion.transitionSelected
                                                   ? Qt.rgba(Theme.transitionOverlap.r, Theme.transitionOverlap.g,
                                                             Theme.transitionOverlap.b, 0.85)
                                                   : (transitionRegion.hasTransition
                                                      ? Qt.rgba(Theme.transitionOverlap.r, Theme.transitionOverlap.g,
                                                                Theme.transitionOverlap.b, 0.65)
                                                      : Qt.rgba(Theme.transitionOverlap.r, Theme.transitionOverlap.g,
                                                                Theme.transitionOverlap.b, 0.35))
                                            border.width: transitionRegion.transitionSelected ? 2 : 1
                                            border.color: Theme.transitionOverlap

                                            // Diagonal hatch so overlaps read as a blend zone.
                                            Canvas {
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                opacity: 0.35
                                                onPaint: {
                                                    const ctx = getContext("2d")
                                                    ctx.clearRect(0, 0, width, height)
                                                    ctx.strokeStyle = Theme.onMedia
                                                    ctx.lineWidth = 1
                                                    const step = 6
                                                    for (let x = -height; x < width; x += step) {
                                                        ctx.beginPath()
                                                        ctx.moveTo(x, height)
                                                        ctx.lineTo(x + height, 0)
                                                        ctx.stroke()
                                                    }
                                                }
                                                onWidthChanged: requestPaint()
                                                onHeightChanged: requestPaint()
                                            }

                                            Text {
                                                anchors.centerIn: parent
                                                visible: parent.width >= 28
                                                text: transitionRegion.hasTransition
                                                      ? (transitionRegion.transitionData.label
                                                         ? transitionRegion.transitionData.label.charAt(0)
                                                         : "≫")
                                                      : "≫"
                                                color: Theme.onMedia
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeTiny
                                                font.weight: Font.Bold
                                            }

                                            ThemedToolTip {
                                                visible: transitionMouse.containsMouse
                                                         && transitionRegion.hasTransition
                                                delay: 400
                                                text: transitionRegion.transitionData.label
                                                      || transitionRegion.transitionData.kind
                                                      || ""
                                            }
                                        }

                                        DropArea {
                                            anchors.fill: parent
                                            keys: ["application/x-drift-transition"]
                                            onDropped: (drop) => {
                                                drop.accept(Qt.CopyAction)
                                                const kind = drop.getDataAsString("application/x-drift-transition")
                                                if (kind.length > 0)
                                                    EditorState.addTransition(trackRow.trackIndex,
                                                                              transitionRegion.leftClipIndex,
                                                                              kind, 0.5)
                                            }
                                        }

                                        MouseArea {
                                            id: transitionMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (transitionRegion.hasTransition) {
                                                    EditorState.selectTransition(trackRow.trackIndex,
                                                                                 transitionRegion.leftClipIndex)
                                                } else {
                                                    EditorState.addTransition(trackRow.trackIndex,
                                                                              transitionRegion.leftClipIndex,
                                                                              "crossfade", 0.5)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // CapCut-style selection box drawn while marquee-dragging.
                    Rectangle {
                        visible: root.marqueeActive
                                 && (root.marqueeWidth > 2 || root.marqueeHeight > 2)
                        x: root.marqueeLeft
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                           + root.marqueeTop
                        width: root.marqueeWidth
                        height: root.marqueeHeight
                        z: 8
                        color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.12)
                        border.width: Theme.borderWidth
                        border.color: Theme.primary
                    }

                    // snap guide -------------------------------------------------------------
                    Rectangle {
                        visible: opacity > 0
                        opacity: root.snapGuideSeconds >= 0 ? 1 : 0
                        x: root.snapGuideSeconds * root.pxPerSecond
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: Theme.borderWidth
                        height: root.totalTracksHeight()
                        color: Theme.snapGuide
                        z: 6

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        // Says what the clip snapped to, instead of leaving a bare
                        // unexplained line on screen.
                        Rectangle {
                            visible: parent.opacity > 0
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            height: snapLabel.implicitHeight + Theme.spacingSm
                            width: snapLabel.implicitWidth + Theme.spacingLg
                            radius: Theme.radiusXs
                            color: Theme.snapGuide

                            Text {
                                id: snapLabel
                                anchors.centerIn: parent
                                text: root.formatTime(root.snapGuideSeconds)
                                color: Theme.overlayColor
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.fontSizeTiny
                            }
                        }
                    }

                    // playhead ---------------------------------------------------------------
                    Item {
                        id: playhead
                        y: 0
                        // Above the seek strip (z: 2), otherwise the scrub area
                        // covers the handle and swallows the press before drag starts.
                        z: 3
                        width: Theme.playheadLineWidth
                        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                                + root.totalTracksHeight()

                        Binding {
                            target: playhead
                            property: "x"
                            value: EditorState.playheadSeconds * root.pxPerSecond
                            when: !playheadDragArea.drag.active && !playheadLineDrag.drag.active
                        }

                        // Follow either drag live so the preview scrubs with it.
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

                        // CapCut/Premiere-style scrubber head in the seek strip.
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

                            // Pointed tip so the head reads as a scrubber, not a knob.
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

                        // Wide grab across the seek strip head.
                        MouseArea {
                            id: playheadDragArea
                            width: Theme.playheadSeekGrabWidth
                            height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                            x: -(width - Theme.playheadLineWidth) / 2
                            y: 0
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
                            hoverEnabled: true
                            drag.target: playhead
                            drag.axis: Drag.XAxis
                            drag.threshold: 0
                            drag.minimumX: 0
                            drag.maximumX: flick.contentWidth - Theme.playheadLineWidth
                            onReleased: playhead.finishSeek()
                        }

                        // Narrow grab down the timeline line — stays thin so clip
                        // clicks aren't stolen.
                        MouseArea {
                            id: playheadLineDrag
                            width: 7
                            height: parent.height - playheadDragArea.height
                            x: -(width - Theme.playheadLineWidth) / 2
                            y: playheadDragArea.height
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
                            drag.target: playhead
                            drag.axis: Drag.XAxis
                            drag.threshold: 0
                            drag.minimumX: 0
                            drag.maximumX: flick.contentWidth - Theme.playheadLineWidth
                            onReleased: playhead.finishSeek()
                        }
                    }

                    // cut tools ----------------------------------------------------------------
                    // Sits above the tracks and playhead so it both draws the red
                    // dashed virtual playhead (and, for the trim tools, the red
                    // gradient over the doomed side) and intercepts the click.
                    Item {
                        id: cutOverlay
                        visible: root.timelineTool !== ""
                        enabled: root.timelineTool !== ""
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        height: root.totalTracksHeight()
                        z: 20

                        readonly property bool trimMode: root.timelineTool === "trimStart"
                                                         || root.timelineTool === "trimEnd"
                        readonly property var hoverClip: (root.cutHoverTrack >= 0 && root.cutHoverClip >= 0
                            && root.cutHoverTrack < root.tracks.length
                            && root.cutHoverClip < root.tracks[root.cutHoverTrack].clips.length)
                            ? root.tracks[root.cutHoverTrack].clips[root.cutHoverClip]
                            : null

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
                            id: cutMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.CrossCursor
                            acceptedButtons: Qt.LeftButton
                            onEntered: cutOverlay.updateHover(mouseX, mouseY)
                            onPositionChanged: (mouse) => cutOverlay.updateHover(mouse.x, mouse.y)
                            onExited: {
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
                                if (root.timelineTool === "trimStart")
                                    EditorState.splitClipLeftAt(trackIdx, clipIdx, atSeconds)
                                else if (root.timelineTool === "trimEnd")
                                    EditorState.splitClipRightAt(trackIdx, clipIdx, atSeconds)
                                else
                                    EditorState.splitClipAt(trackIdx, clipIdx, atSeconds)
                            }
                        }

                        // Red gradient on the doomed side of the hovered clip: a
                        // fixed band attached to the cut line, reddest at the
                        // pointer and fading away from it, clamped to the clip.
                        Rectangle {
                            id: doomGradient
                            readonly property real bandLength: 80
                            visible: cutOverlay.trimMode && cutOverlay.hoverClip !== null
                                     && root.cutHoverSeconds >= 0
                            readonly property real clipStartX: cutOverlay.hoverClip
                                ? cutOverlay.hoverClip.start * root.pxPerSecond : 0
                            readonly property real clipEndX: cutOverlay.hoverClip
                                ? (cutOverlay.hoverClip.start + cutOverlay.hoverClip.duration) * root.pxPerSecond : 0
                            readonly property real cutX: Math.max(clipStartX,
                                Math.min(clipEndX, root.cutHoverSeconds * root.pxPerSecond))
                            readonly property color solid: Qt.rgba(Theme.destructive.r, Theme.destructive.g,
                                                                   Theme.destructive.b, 0.55)
                            readonly property color clear: Qt.rgba(Theme.destructive.r, Theme.destructive.g,
                                                                   Theme.destructive.b, 0.0)

                            x: root.timelineTool === "trimStart"
                               ? Math.max(clipStartX, cutX - bandLength)
                               : cutX
                            width: root.timelineTool === "trimStart"
                                   ? cutX - x
                                   : Math.min(clipEndX, cutX + bandLength) - cutX
                            y: root.trackOffsetY(root.cutHoverTrack)
                            height: cutOverlay.hoverClip
                                ? root.trackHeight(root.cutHoverTrack) : 0
                            radius: Theme.radiusSm

                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                // Red toward the cut line (the pointer), fading away.
                                GradientStop {
                                    position: 0.0
                                    color: root.timelineTool === "trimStart"
                                           ? doomGradient.clear : doomGradient.solid
                                }
                                GradientStop {
                                    position: 1.0
                                    color: root.timelineTool === "trimStart"
                                           ? doomGradient.solid : doomGradient.clear
                                }
                            }
                        }

                        // Red dashed virtual playhead. A column of dashes reflows
                        // with the track height without any repaint plumbing.
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
        } // Column (keyframes + tracks)
    }

    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() {
            if (EditorState.playing)
                root.ensurePlayheadVisible()
        }
    }
}

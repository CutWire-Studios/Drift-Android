import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Transform overlay: resize/rotate grips and the in-place text editor for the
// clips visible at the playhead. Sits outside the (clipped) canvas rect,
// mirroring its geometry, so grips on a clip that runs past a canvas edge stay
// drawn and grabbable instead of being cut away with the frame. Geometry
// (x/y/width/height/z/visible) is driven by the owning PreviewPanel.
Item {
    id: root

    property var overlayClips: []
    // True while a handle is being dragged. Rebuilding the model
    // mid-drag would destroy the delegate that owns the active
    // grab, so refreshes are suppressed until the drag ends.
    // (Also held true while a text clip is edited in place, so
    // the inline editor delegate is not destroyed mid-edit.)
    property bool interacting: false

    // "track:clip" of the text clip currently edited in place,
    // or "" when no inline edit is active.
    property string editingKey: ""

    // "track:clip" of a freshly added placeholder text clip that
    // should open its editor as soon as its box exists.
    property string pendingEditKey: ""

    function refreshOverlay() {
        if (interacting)
            return
        // Playhead ticks ~60 Hz; rebuilding grips every tick during playback
        // is wasted work and stalls the UI on long timelines.
        if (EditorState.playing)
            return
        overlayClips = EditorState.previewClipsAtPlayhead()
    }

    function endInteraction() {
        EditorState.commitPreviewDrag()
        interacting = false
        Qt.callLater(refreshOverlay)
    }

    Component.onCompleted: refreshOverlay()

    Connections {
        target: EditorState
        function onTracksChanged() { root.refreshOverlay() }
        function onSelectionChanged() { root.refreshOverlay() }
        function onPlayheadSecondsChanged() { root.refreshOverlay() }
        function onPlayingChanged() {
            if (!EditorState.playing)
                root.refreshOverlay()
        }
        function onInlineTextEditRequested(trackIndex, clipIndex) {
            root.pendingEditKey = trackIndex + ":" + clipIndex
            root.refreshOverlay()
        }
    }

    Repeater {
        model: root.overlayClips

        delegate: Item {
            id: handle
            required property var modelData

            readonly property bool selected: EditorState.selectedTrack === modelData.track
                                                    && EditorState.selectedClip === modelData.clip
            readonly property bool isText: modelData.kind === "text"
            readonly property bool editing: root.editingKey
                                                    === (modelData.track + ":" + modelData.clip)
            // True when this clip was just added with no text and should
            // open with an empty editor instead of the placeholder string.
            property bool openAsPlaceholder: false
            // Live clip style — tracks EditorState so property-panel edits
            // apply to the inline editor in real time.
            readonly property var liveStyle: {
                void EditorState.tracks
                const info = EditorState.clipAt(modelData.track, modelData.clip)
                return info && info.textStyle ? info.textStyle : null
            }
            readonly property var liveClip: {
                void EditorState.tracks
                return EditorState.clipAt(modelData.track, modelData.clip)
            }
            readonly property string liveText: liveClip ? (liveClip.textContent || "") : ""

            function enterEdit() {
                root.editingKey = modelData.track + ":" + modelData.clip
                root.interacting = true
                EditorState.selectClip(modelData.track, modelData.clip)
                EditorState.beginTextEdit(modelData.track, modelData.clip)
                const info = EditorState.clipAt(modelData.track, modelData.clip)
                if (handle.openAsPlaceholder) {
                    editor.text = ""
                    EditorState.previewSetClipTextContent(modelData.track, modelData.clip, "")
                    handle.openAsPlaceholder = false
                } else {
                    editor.text = info.textContent || ""
                }
                editor.forceActiveFocus()
                editor.selectAll()
            }

            function commitEdit() {
                if (!handle.editing)
                    return
                EditorState.commitTextEdit(modelData.track, modelData.clip, editor.text)
                handle.finishEdit()
            }

            function cancelEdit() {
                if (!handle.editing)
                    return
                EditorState.cancelPreviewDrag()
                handle.finishEdit()
            }

            function finishEdit() {
                root.editingKey = ""
                EditorState.endTextEdit()
                root.interacting = false
                Qt.callLater(root.refreshOverlay)
            }

            // A just-added placeholder clip opens its own editor.
            // Checked both on creation and when the key arrives,
            // since either can happen first.
            function claimPendingEdit() {
                if (!handle.isText || handle.editing)
                    return
                if (root.pendingEditKey
                        !== (modelData.track + ":" + modelData.clip))
                    return
                root.pendingEditKey = ""
                handle.openAsPlaceholder = true
                Qt.callLater(handle.enterEdit)
            }

            Component.onCompleted: handle.claimPendingEdit()

            Connections {
                target: root
                function onPendingEditKeyChanged() { handle.claimPendingEdit() }
            }

            readonly property real canvasW: Math.max(1, modelData.canvasWidth)
            readonly property real canvasH: Math.max(1, modelData.canvasHeight)
            readonly property real sx: parent.width / canvasW
            readonly property real sy: parent.height / canvasH

            // Live overrides applied during a drag so the box tracks
            // the cursor without rebuilding the (stale) model.
            property real liveX: -1e12
            property real liveY: -1e12
            property real liveW: -1
            property real liveH: -1
            property real liveRotation: 1e9

            readonly property real layoutX: liveX > -1e11 ? liveX : modelData.x
            readonly property real layoutY: liveY > -1e11 ? liveY : modelData.y
            readonly property real layoutW: liveW >= 0 ? liveW : modelData.width
            readonly property real layoutH: liveH >= 0 ? liveH : modelData.height
            readonly property real centerX: (layoutX + layoutW * 0.5) * sx
            readonly property real centerY: (layoutY + layoutH * 0.5) * sy

            x: layoutX * sx
            y: layoutY * sy
            width: Math.max(24, layoutW * sx)
            height: Math.max(24, layoutH * sy)
            // Front-most track (lowest index) sits on top so it
            // wins click hit-testing over boxes behind it. The clip
            // being edited jumps above everything so its editor and
            // the click-away catcher order correctly.
            z: handle.editing ? 1000 : -modelData.track
            transformOrigin: Item.Center
            rotation: liveRotation < 1e8 ? liveRotation : modelData.rotation

            property real dragStartX: 0
            property real dragStartY: 0
            property real dragStartW: 1
            property real dragStartH: 1
            property int dragStartPixelSize: 64

            // Arrow keys move the selected clip; Shift makes
            // the step coarse. Transform used to be
            // drag-only, with no keyboard path at all.
            focus: handle.selected && !handle.editing
            Keys.onPressed: function(event) {
                if (!handle.selected || handle.editing)
                    return
                const step = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
                let dx = 0
                let dy = 0
                switch (event.key) {
                case Qt.Key_Left:  dx = -step; break
                case Qt.Key_Right: dx = step; break
                case Qt.Key_Up:    dy = -step; break
                case Qt.Key_Down:  dy = step; break
                default: return
                }
                EditorState.beginPreviewDrag()
                EditorState.previewSetClipPosition(
                    handle.modelData.track,
                    handle.modelData.clip,
                    handle.modelData.x + dx,
                    handle.modelData.y + dy)
                EditorState.commitPreviewDrag()
                event.accepted = true
            }

            Connections {
                target: EditorState
                function onSelectedClipDataChanged() {
                    if (!handle.editing || editor.activeFocus)
                        return
                    const info = EditorState.clipAt(modelData.track, modelData.clip)
                    if (!info)
                        return
                    const next = info.textContent || ""
                    if (editor.text !== next)
                        editor.text = next
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: handle.isText && (handle.selected || handle.editing)
                         && handle.liveStyle && handle.liveStyle.boxEnabled
                color: handle.liveStyle ? handle.liveStyle.boxColor : "transparent"
                radius: handle.liveStyle ? handle.liveStyle.boxRadius * handle.sy : 0
            }

            // A plain Text item cannot show per-word accents, highlight pills or
            // underlines, so styles that use them fall back to the composited raster
            // rather than preview something the export will not match.
            readonly property bool plainStyle: !handle.liveStyle
                    || ((!handle.liveStyle.accent || handle.liveStyle.accent.rule === "none")
                        && !(handle.liveStyle.wordHighlight
                             && handle.liveStyle.wordHighlight.enabled)
                        && !handle.liveStyle.underlineEnabled)

            // Crisp vector text while the clip is selected. The composited
            // raster is downscaled for preview and looks soft when upscaled.
            Text {
                anchors.fill: parent
                visible: handle.isText && handle.selected && !handle.editing
                         && handle.plainStyle
                text: handle.liveText
                renderType: Text.NativeRendering
                color: handle.liveStyle ? handle.liveStyle.color : "white"
                font.family: handle.liveStyle ? handle.liveStyle.fontFamily : Theme.fontFamily
                font.pixelSize: handle.liveStyle
                                ? Math.max(1, Math.round(handle.liveStyle.pixelSize * handle.sy))
                                : 16
                font.weight: handle.liveStyle ? handle.liveStyle.fontWeight : Font.Normal
                font.italic: handle.liveStyle ? handle.liveStyle.italic : false
                font.letterSpacing: handle.liveStyle ? handle.liveStyle.letterSpacing * handle.sy : 0
                wrapMode: (handle.liveStyle && handle.liveStyle.wordWrap === false)
                          ? Text.NoWrap : Text.WordWrap
                horizontalAlignment: !handle.liveStyle ? Text.AlignHCenter
                                     : handle.liveStyle.align === "left" ? Text.AlignLeft
                                     : handle.liveStyle.align === "right" ? Text.AlignRight
                                     : Text.AlignHCenter
                verticalAlignment: !handle.liveStyle ? Text.AlignVCenter
                                   : handle.liveStyle.valign === "top" ? Text.AlignTop
                                   : handle.liveStyle.valign === "bottom" ? Text.AlignBottom
                                   : Text.AlignVCenter
                leftPadding: handle.liveStyle && handle.liveStyle.boxEnabled
                             ? Math.max(0, handle.liveStyle.boxPadding * handle.sy) : 0
                rightPadding: leftPadding
                topPadding: leftPadding
                bottomPadding: leftPadding
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: (handle.selected || handle.editing)
                              ? Theme.borderWidthFocus : Theme.borderWidth
                border.color: handle.selected ? Theme.primary : Theme.guideStrong
                radius: Theme.radiusXs

                Behavior on border.width {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
                Behavior on border.color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
            }

            // In-place text editor (Canva-style). Shown over the
            // box while editing; the baked raster is hidden by the
            // compositor via beginTextEdit. Plain TextArea (not a
            // Themed* control): this is canvas content that must
            // match the rendered text, not app chrome.
            TextArea {
                id: editor
                anchors.fill: parent
                visible: handle.editing
                enabled: handle.editing
                background: null
                renderType: TextEdit.NativeRendering
                padding: handle.liveStyle && handle.liveStyle.boxEnabled
                          ? Math.max(0, handle.liveStyle.boxPadding * handle.sy) : 0
                selectByMouse: true
                color: handle.liveStyle ? handle.liveStyle.color : "white"
                font.family: handle.liveStyle ? handle.liveStyle.fontFamily : Theme.fontFamily
                font.pixelSize: handle.liveStyle
                                ? Math.max(1, Math.round(handle.liveStyle.pixelSize * handle.sy))
                                : 16
                font.weight: handle.liveStyle ? handle.liveStyle.fontWeight : Font.Normal
                font.italic: handle.liveStyle ? handle.liveStyle.italic : false
                font.letterSpacing: handle.liveStyle ? handle.liveStyle.letterSpacing * handle.sy : 0
                wrapMode: (handle.liveStyle && handle.liveStyle.wordWrap === false)
                          ? TextEdit.NoWrap : TextEdit.WordWrap
                horizontalAlignment: !handle.liveStyle ? TextEdit.AlignHCenter
                                     : handle.liveStyle.align === "left" ? TextEdit.AlignLeft
                                     : handle.liveStyle.align === "right" ? TextEdit.AlignRight
                                     : TextEdit.AlignHCenter
                verticalAlignment: !handle.liveStyle ? TextEdit.AlignVCenter
                                   : handle.liveStyle.valign === "top" ? TextEdit.AlignTop
                                   : handle.liveStyle.valign === "bottom" ? TextEdit.AlignBottom
                                   : TextEdit.AlignVCenter
                onTextChanged: {
                    if (!handle.editing)
                        return
                    EditorState.previewSetClipTextContent(
                        handle.modelData.track,
                        handle.modelData.clip,
                        text)
                }
                Keys.onEscapePressed: handle.cancelEdit()
                onActiveFocusChanged: if (!activeFocus && handle.editing) handle.commitEdit()
            }

            TapHandler {
                enabled: !handle.editing
                onTapped: EditorState.selectClip(handle.modelData.track, handle.modelData.clip)
                onDoubleTapped: if (handle.isText) handle.enterEdit()
            }

            DragHandler {
                target: null
                enabled: !handle.editing
                cursorShape: Qt.SizeAllCursor
                onActiveChanged: {
                    if (active) {
                        root.interacting = true
                        handle.dragStartX = handle.modelData.x
                        handle.dragStartY = handle.modelData.y
                        handle.liveX = handle.dragStartX
                        handle.liveY = handle.dragStartY
                        EditorState.selectClip(handle.modelData.track, handle.modelData.clip)
                        EditorState.beginPreviewDrag()
                    } else {
                        handle.liveX = -1e12
                        handle.liveY = -1e12
                        root.endInteraction()
                    }
                }
                onTranslationChanged: {
                    // translation is in the rotated box frame; rotate it back to canvas axes
                    const a = handle.rotation * Math.PI / 180
                    const dx = translation.x * Math.cos(a) - translation.y * Math.sin(a)
                    const dy = translation.x * Math.sin(a) + translation.y * Math.cos(a)
                    const xPx = handle.dragStartX + dx / handle.sx
                    const yPx = handle.dragStartY + dy / handle.sy
                    handle.liveX = xPx
                    handle.liveY = yPx
                    EditorState.previewSetClipPosition(
                        handle.modelData.track,
                        handle.modelData.clip,
                        xPx,
                        yPx)
                }
            }

            // Corner resize handles (opposite corner stays fixed)
            Repeater {
                model: (handle.selected && !handle.editing) ? 4 : 0

                delegate: Rectangle {
                    id: corner
                    required property int index
                    // 0 TL, 1 TR, 2 BL, 3 BR
                    readonly property real sxSign: (index === 0 || index === 2) ? -1 : 1
                    readonly property real sySign: (index < 2) ? -1 : 1

                    width: 12
                    height: 12
                    radius: 2
                    color: Theme.primary
                    border.width: 1
                    border.color: Theme.onMedia
                    x: (sxSign < 0 ? 0 : handle.width) - width / 2
                    y: (sySign < 0 ? 0 : handle.height) - height / 2

                    DragHandler {
                        id: cornerDrag
                        target: null
                        cursorShape: (corner.sxSign * corner.sySign < 0) ? Qt.SizeBDiagCursor : Qt.SizeFDiagCursor
                        onActiveChanged: {
                            if (active) {
                                handle.dragStartX = handle.layoutX
                                handle.dragStartY = handle.layoutY
                                handle.dragStartW = handle.layoutW
                                handle.dragStartH = handle.layoutH
                                handle.dragStartPixelSize = handle.modelData.pixelSize || 64
                                handle.liveX = handle.dragStartX
                                handle.liveY = handle.dragStartY
                                handle.liveW = handle.dragStartW
                                handle.liveH = handle.dragStartH
                                root.interacting = true
                                EditorState.selectClip(handle.modelData.track, handle.modelData.clip)
                                EditorState.beginPreviewDrag()
                            } else {
                                handle.liveX = -1e12
                                handle.liveY = -1e12
                                handle.liveW = -1
                                handle.liveH = -1
                                root.endInteraction()
                            }
                        }
                        onCentroidChanged: {
                            if (!active)
                                return
                            const p = root.mapFromItem(null, cornerDrag.centroid.scenePosition.x,
                                                                    cornerDrag.centroid.scenePosition.y)
                            const px = p.x / handle.sx
                            const py = p.y / handle.sy
                            const right = handle.dragStartX + handle.dragStartW
                            const bottom = handle.dragStartY + handle.dragStartH
                            let x = handle.dragStartX
                            let y = handle.dragStartY
                            let w = handle.dragStartW
                            let h = handle.dragStartH
                            if (corner.index === 0) { // TL
                                x = Math.min(px, right - 1)
                                y = Math.min(py, bottom - 1)
                                w = right - x
                                h = bottom - y
                            } else if (corner.index === 1) { // TR
                                y = Math.min(py, bottom - 1)
                                w = Math.max(1, px - handle.dragStartX)
                                h = bottom - y
                            } else if (corner.index === 2) { // BL
                                x = Math.min(px, right - 1)
                                w = right - x
                                h = Math.max(1, py - handle.dragStartY)
                            } else { // BR
                                w = Math.max(1, px - handle.dragStartX)
                                h = Math.max(1, py - handle.dragStartY)
                            }
                            handle.liveX = x
                            handle.liveY = y
                            handle.liveW = w
                            handle.liveH = h
                            if (handle.modelData.kind === "text") {
                                // Height drives the glyph scale; a width-only drag
                                // just re-wraps, since the box is the wrap width.
                                const px = Math.round(handle.dragStartPixelSize
                                                      * h / Math.max(1, handle.dragStartH))
                                EditorState.previewSetTextRect(
                                    handle.modelData.track,
                                    handle.modelData.clip,
                                    x, y, w, h, px)
                            } else {
                                EditorState.previewSetClipRect(
                                    handle.modelData.track,
                                    handle.modelData.clip,
                                    x, y, w, h)
                            }
                        }
                    }
                }
            }

            // Rotation handle above the box
            Item {
                visible: handle.selected && !handle.editing
                width: 14
                height: 14
                x: handle.width / 2 - width / 2
                y: -28

                Rectangle {
                    anchors.top: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 1
                    height: 16
                    color: Theme.primary
                }

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: Theme.primary
                    border.width: 1
                    border.color: Theme.onMedia
                }

                DragHandler {
                    id: rotateDrag
                    target: null
                    cursorShape: Qt.CrossCursor
                    onActiveChanged: {
                        if (active) {
                            root.interacting = true
                            handle.liveRotation = handle.modelData.rotation
                            EditorState.selectClip(handle.modelData.track, handle.modelData.clip)
                            EditorState.beginPreviewDrag()
                        } else {
                            handle.liveRotation = 1e9
                            root.endInteraction()
                        }
                    }
                    onCentroidChanged: {
                        if (!active)
                            return
                        const p = root.mapFromItem(null, rotateDrag.centroid.scenePosition.x,
                                                                 rotateDrag.centroid.scenePosition.y)
                        const ang = Math.atan2(p.y - handle.centerY, p.x - handle.centerX)
                        const deg = ang * 180 / Math.PI + 90
                        handle.liveRotation = deg
                        EditorState.previewSetClipRotation(
                            handle.modelData.track,
                            handle.modelData.clip,
                            deg)
                    }
                }
            }
        }
    }

    // Click-away catcher: while a text clip is edited in place,
    // a press outside the (raised) editor box drops focus, which
    // commits the edit via the editor's onActiveFocusChanged.
    MouseArea {
        anchors.fill: parent
        z: 500
        visible: root.editingKey !== ""
        enabled: visible
        onPressed: (mouse) => {
            root.forceActiveFocus()
            mouse.accepted = true
        }
    }
}

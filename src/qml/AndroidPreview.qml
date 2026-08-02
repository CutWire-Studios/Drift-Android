import QtQuick
import Drift
import "components"

// CapCut-style phone preview: letterboxed canvas + compact transport.
// No transform/crop overlays, guides, or fullscreen — those stay deferred.
Item {
    id: root

    readonly property real currentSeconds: EditorState.playheadSeconds
    readonly property real durationSeconds: EditorState.durationSeconds
    readonly property bool playing: EditorState.playing

    readonly property int projectFps: {
        void EditorState.tracks
        const fps = EditorState.projectFps()
        return fps > 0 ? fps : 30
    }

    function formatTimecode(seconds) {
        const fps = root.projectFps
        const totalFrames = Math.round(seconds * fps)
        const h = Math.floor(totalFrames / (fps * 3600))
        const m = Math.floor(totalFrames / (fps * 60)) % 60
        const s = Math.floor(totalFrames / fps) % 60
        const f = totalFrames % fps
        function pad(n) { return n.toString().padStart(2, "0") }
        return pad(h) + ":" + pad(m) + ":" + pad(s) + ":" + pad(f)
    }

    // Cap preferred height so a portrait project canvas cannot starve the timeline.
    // The editor SplitView may assign a taller/shorter explicit height when dragged.
    readonly property real maxPreviewBodyHeight: {
        const h = (parent && parent.height > 0) ? parent.height : 800
        return h * Theme.androidPreviewMaxScreenFraction
    }

    implicitHeight: Math.min(maxPreviewBodyHeight, preferredBodyHeight)
                    + Theme.androidPreviewTransportHeight
    readonly property real preferredBodyHeight: {
        const aspect = viewport.aspect
        if (aspect <= 0 || width <= 0)
            return Math.min(maxPreviewBodyHeight, 200)
        return Math.min(maxPreviewBodyHeight, width / aspect)
    }

    Column {
        anchors.fill: parent

        Item {
            id: viewportOuter
            width: parent.width
            // Fill the height the splitter (or implicitHeight) allocated, minus transport.
            height: Math.max(0, parent.height - Theme.androidPreviewTransportHeight)
            clip: true

            Item {
                id: viewport
                anchors.fill: parent
                anchors.margins: Theme.spacingSm

                property real aspect: {
                    void EditorState.tracks
                    const w = EditorState.projectWidth()
                    const h = EditorState.projectHeight()
                    return (w > 0 && h > 0) ? (w / h) : (16 / 9)
                }

                readonly property real fitWidth: Math.min(width, height * aspect)
                readonly property real fitHeight: fitWidth / aspect

                Rectangle {
                    id: canvasRect
                    width: viewport.fitWidth
                    height: viewport.fitHeight
                    x: (viewport.width - width) / 2
                    y: (viewport.height - height) / 2
                    color: Theme.overlayColor
                    border.width: Theme.borderWidth
                    border.color: Theme.border
                    clip: true

                    PreviewItem {
                        id: preview
                        anchors.fill: parent
                        playback: EditorState.playback

                        function updateRenderSize() {
                            EditorState.playback.setPreviewRenderSize(
                                Math.round(width), Math.round(height))
                        }

                        Component.onCompleted: updateRenderSize()
                        onWidthChanged: updateRenderSize()
                        onHeightChanged: updateRenderSize()
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: opacity > 0
                        opacity: EditorState.playback.hasFrame ? 0 : 1
                        text: EditorState.activeAudioClipAtPlayhead().path
                              ? qsTr("Audio only") : qsTr("No clip at the current time")
                        color: Theme.guideMedium
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                        }
                    }
                }
            }
        }

        Item {
            id: transport
            width: parent.width
            height: Theme.androidPreviewTransportHeight

            Text {
                id: timecodeLabel
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingXl
                anchors.verticalCenter: parent.verticalCenter
                text: root.formatTimecode(root.currentSeconds) + " / "
                      + root.formatTimecode(root.durationSeconds)
                color: Theme.mutedForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            IconButton {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: root.playing ? Theme.icons.pause : Theme.icons.play
                variant: "text"
                tooltip: root.playing ? qsTr("Pause") : qsTr("Play")
                onClicked: EditorState.togglePlayback()
            }
        }
    }

    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() {
            if (!EditorState.playing)
                EditorState.playback.refreshFrame()
        }
        function onTracksChanged() {
            EditorState.playback.refreshFrame()
        }
        function onPlayingChanged() {
            if (!EditorState.playing)
                EditorState.playback.refreshFrame()
        }
    }

    Component.onCompleted: EditorState.playback.refreshFrame()
}

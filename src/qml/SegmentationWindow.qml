import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Drift 1.0
import "components"

// Prompting surface for SAM2 segmentation. Opened for one clip: pick a reference frame, mark the
// subject, then run the pass over the whole clip.
Window {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property real clipStartSeconds: 0
    property real clipDurationSeconds: 0

    // Phone form factor. The desktop layout reserves a fixed 260px sidebar beside the
    // stage, which on a 360dp screen leaves the frame about 80px to be prompted on.
    readonly property bool compact: width < 640
    // Touch has no second mouse button, so include/exclude becomes an explicit mode.
    property bool includeMode: true

    width: 1100
    height: 720
    minimumWidth: 780
    minimumHeight: 520
    title: qsTr("Cut out subject")
    color: Theme.appBackground

    // Android hands a secondary top-level window the whole display and gives it no
    // frame, so the desktop size above would leave it laid out for a screen it does
    // not have. Asked for on show rather than bound, because show() drives visibility
    // itself and would overwrite a binding.
    onVisibleChanged: if (visible && Qt.platform.os === "android") visibility = Window.FullScreen

    function openFor(track, clip, startSeconds, durationSeconds, sessionAlreadyStarted) {
        root.trackIndex = track
        root.clipIndex = clip
        root.clipStartSeconds = startSeconds
        root.clipDurationSeconds = durationSeconds
        frameSlider.value = 0
        if (!sessionAlreadyStarted)
            EditorState.beginSegmentationSession(track, clip, startSeconds)
        root.show()
        root.raise()
        root.requestActivate()
    }

    onClosing: EditorState.endSegmentationSession()

    Connections {
        target: EditorState
        function onSegmentationFinished(ok, message) {
            if (ok)
                root.close()
        }
    }

    Grid {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg
        // One column stacks stage over controls on a phone; two puts them side by side.
        columns: root.compact ? 1 : 2
        rows: root.compact ? 2 : 1

        // ----- Frame + prompt overlay ------------------------------------------------------
        Column {
            width: root.compact ? parent.width
                                : parent.width - sidebar.width - Theme.spacingLg
            height: root.compact
                    ? Math.max(0, parent.height - sidebar.height - Theme.spacingLg)
                    : parent.height
            spacing: Theme.spacingMd

            Item {
                id: stage
                width: parent.width
                height: parent.height - scrubRow.height - Theme.spacingMd

                readonly property real frameW: EditorState.segmentFrameSize.width
                readonly property real frameH: EditorState.segmentFrameSize.height
                readonly property real aspect: frameH > 0 ? frameW / frameH : 16 / 9

                // Letterboxed fit, and the single source of truth for mapping clicks back into
                // normalized frame coordinates.
                readonly property real fitW: Math.min(width, height * aspect)
                readonly property real fitH: fitW / aspect
                readonly property real fitX: (width - fitW) / 2
                readonly property real fitY: (height - fitH) / 2

                Rectangle {
                    anchors.fill: parent
                    color: Theme.panelBackground
                    radius: Theme.radiusMd
                }

                Image {
                    id: frameImage
                    x: stage.fitX
                    y: stage.fitY
                    width: stage.fitW
                    height: stage.fitH
                    fillMode: Image.Stretch
                    cache: false
                    // The revision defeats QML's URL-keyed image cache; the pixels behind these
                    // URLs change on every prompt edit.
                    source: EditorState.segmentSessionActive
                            ? "image://segment/frame?rev=" + EditorState.segmentRevision
                            : ""
                }

                Image {
                    id: maskImage
                    x: stage.fitX
                    y: stage.fitY
                    width: stage.fitW
                    height: stage.fitH
                    fillMode: Image.Stretch
                    cache: false
                    opacity: 0.45
                    source: EditorState.segmentSessionActive
                            ? "image://segment/mask?rev=" + EditorState.segmentRevision
                            : ""
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    enabled: EditorState.segmentSessionActive && !EditorState.segmentEncoding
                             && !EditorState.segmenting
                    onClicked: function (mouse) {
                        const nx = (mouse.x - stage.fitX) / stage.fitW
                        const ny = (mouse.y - stage.fitY) / stage.fitH
                        if (nx < 0 || nx > 1 || ny < 0 || ny > 1)
                            return
                        EditorState.addSegmentationPoint(
                            nx, ny,
                            root.compact ? root.includeMode : mouse.button === Qt.LeftButton)
                    }
                }

                // Prompt markers. Green includes the subject, red carves it out; click one to drop it.
                Repeater {
                    model: EditorState.segmentPoints
                    delegate: Rectangle {
                        required property int index
                        required property var modelData

                        width: 14
                        height: 14
                        radius: 7
                        x: stage.fitX + modelData.x * stage.fitW - 7
                        y: stage.fitY + modelData.y * stage.fitH - 7
                        color: modelData.include ? Theme.constructive : Theme.destructive
                        border.width: 2
                        border.color: Theme.primaryForeground

                        MouseArea {
                            anchors.fill: parent
                            // The 14px dot is the mark, not the target.
                            anchors.margins: Theme.touchUi ? -13 : 0
                            enabled: !EditorState.segmenting
                            onClicked: EditorState.removeSegmentationPoint(parent.index)
                        }
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    visible: EditorState.segmentEncoding
                    width: encodingLabel.width + Theme.spacingXl
                    height: encodingLabel.height + Theme.spacingLg
                    radius: Theme.radiusMd
                    color: Theme.scrimStrong

                    ThemedLabel {
                        id: encodingLabel
                        anchors.centerIn: parent
                        text: qsTr("Looking at this moment…")
                    }
                }
            }

            Row {
                id: scrubRow
                width: parent.width
                spacing: Theme.spacingMd

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Frame")
                }

                ThemedSlider {
                    id: frameSlider
                    label: qsTr("Frame")
                    width: Math.max(80, parent.width - (root.compact ? 120 : 220))
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0
                    to: Math.max(0.001, root.clipDurationSeconds)
                    enabled: !EditorState.segmentEncoding && !EditorState.segmenting
                    // Re-encoding on every slider tick would queue seconds of work per drag.
                    onPressedChanged: {
                        if (!pressed)
                            EditorState.setSegmentationFrame(root.clipStartSeconds + value)
                    }
                }

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: frameSlider.value.toFixed(2) + qsTr("s")
                }
            }
        }

        // ----- Controls --------------------------------------------------------------------
        // Compact mode caps the sidebar so the stage above stays usable, which means
        // the controls no longer all fit — unscrolled, Cut out and Cancel were simply
        // clipped off the bottom.
        Flickable {
            id: sidebar
            width: root.compact ? parent.width : 260
            height: root.compact
                    ? Math.min(sidebarColumn.implicitHeight, parent.height * 0.42)
                    : parent.height
            contentWidth: width
            contentHeight: sidebarColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: AppScrollBar {
                policy: sidebar.contentHeight > sidebar.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }

            Column {
                id: sidebarColumn
                // Height comes from the content; the Flickable caps and scrolls it, because
                // in compact mode the stage above still needs most of the screen.
                width: parent.width
                spacing: Theme.spacingLg

                ThemedLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.compact
                          ? qsTr("Pick Subject or Exclude, then tap the frame. Tap a marker to remove it.")
                          : qsTr("Left-click marks the subject, right-click marks what to exclude. Click a marker to remove it.")
                }

                // Touch equivalent of the two mouse buttons.
                Row {
                    width: parent.width
                    visible: root.compact
                    spacing: Theme.spacingMd

                    ThemedButton {
                        text: qsTr("Subject")
                        variant: root.includeMode ? "primary" : "secondary"
                        onClicked: root.includeMode = true
                    }
                    ThemedButton {
                        text: qsTr("Exclude")
                        variant: root.includeMode ? "secondary" : "destructive"
                        onClicked: root.includeMode = false
                    }
                }

                ThemedLabel {
                    width: parent.width
                    text: qsTr("AI: %1").arg(EditorState.segmentationModelVariant() || qsTr("not installed"))
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm

                    ThemedLabel { text: qsTr("Result") }

                    ThemedComboBox {
                        id: outputBox
                        width: parent.width
                        visible: !EditorState.segmentationForTemplate
                        enabled: !EditorState.segmenting
                        textRole: "label"
                        valueRole: "value"
                        model: [
                            { label: qsTr("Two clips (subject + background)"), value: "clips" },
                            { label: qsTr("Hide everything except the subject"), value: "mask" }
                        ]
                    }

                    ThemedLabel {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        visible: EditorState.segmentationForTemplate
                        text: qsTr("The cutout is only for this effect — no extra tracks are added.")
                    }
                }

                ThemedButton {
                    width: parent.width
                    variant: "secondary"
                    text: qsTr("Clear points")
                    enabled: EditorState.segmentPoints.length > 0 && !EditorState.segmenting
                    onClicked: EditorState.clearSegmentationPoints()
                }

                ThemedButton {
                    width: parent.width
                    variant: "primary"
                    text: EditorState.segmenting
                          ? qsTr("Cutting out… %1%").arg(Math.round(EditorState.segmentProgress * 100))
                          : (EditorState.segmentationForTemplate
                             ? qsTr("Cut out & apply effect")
                             : qsTr("Cut out subject"))
                    enabled: EditorState.segmentPoints.length > 0 && !EditorState.segmenting
                             && !EditorState.segmentEncoding
                    onClicked: EditorState.runSegmentationSession(
                        EditorState.segmentationForTemplate ? "template" : outputBox.currentValue)
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm
                    visible: EditorState.segmenting

                    LabelledProgressRing {
                        anchors.horizontalCenter: parent.horizontalCenter
                        value: EditorState.segmentProgress
                        indeterminate: EditorState.segmentProgress <= 0
                    }

                    ThemedLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: EditorState.segmentStatus
                    }

                    ThemedButton {
                        width: parent.width
                        variant: "destructive"
                        text: qsTr("Cancel")
                        onClicked: EditorState.cancelSegmentation()
                    }
                }

                ThemedLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    visible: !EditorState.segmenting
                    text: qsTr("Each moment is processed, so longer clips take longer.")
                }
            }
        }
    }

    // ----- Footer ---------------------------------------------------------------------------
    // The only way out on Android, which gives this window no frame — and "Cancel" inside the
    // sidebar cancels the run, not the session.
    ThemedButton {
        id: footer
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
        variant: "ghost"
        text: qsTr("Close")
        onClicked: root.close()
    }
}

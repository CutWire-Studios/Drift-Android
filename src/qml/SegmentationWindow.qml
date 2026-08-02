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

    width: 1100
    height: 720
    minimumWidth: 780
    minimumHeight: 520
    title: qsTr("Cut out subject")
    color: Theme.appBackground

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

    Row {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        // ----- Frame + prompt overlay ------------------------------------------------------
        Column {
            width: parent.width - sidebar.width - Theme.spacingLg
            height: parent.height
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
                        EditorState.addSegmentationPoint(nx, ny, mouse.button === Qt.LeftButton)
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
                    width: parent.width - 220
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
        Column {
            id: sidebar
            width: 260
            height: parent.height
            spacing: Theme.spacingLg

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Left-click marks the subject, right-click marks what to exclude. Click a marker to remove it.")
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

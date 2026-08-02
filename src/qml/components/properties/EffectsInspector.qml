import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

Item {
    id: root

    // Raised by the empty state; Main wires it to the assets panel so
    // "Browse effects" actually takes the user somewhere.
    signal browseEffectsRequested()

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    readonly property var selectedEffects: EditorState.selectedClipEffects

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        // The face warp effects follow baked landmarks, so the clip has to be
        // scanned before any of them do anything. This sits above the effect list
        // because that ordering is the workflow: detect, then apply.
        Column {
            id: faceSection
            visible: root.clipKind === "video"
            width: parent.width
            spacing: Theme.spacingSm

            // The model is an addon, but it can equally come from a bundled
            // models/face or DRIFT_FACE_MODEL_DIR, so ask the engine rather than
            // the addon registry. That answer is not a binding, hence the reset
            // below when an addon of this kind appears.
            // Folded together because every control below is gated on the same
            // answer; runtimeReady is kept apart only to say which half is missing.
            property bool runtimeReady: Addons.runtimeAvailable()
            property bool faceReady: EditorState.faceDetectionAvailable()
                                     && Addons.runtimeAvailable()
            property bool hasTrack: {
                const data = EditorState.selectedClipData
                return data && data.hasFaceTrack === true
            }

            Connections {
                target: Addons
                function onKindChanged(kind) {
                    if (kind !== "face-model" && kind !== "onnxruntime")
                        return
                    faceSection.runtimeReady = Addons.runtimeAvailable()
                    faceSection.faceReady = EditorState.faceDetectionAvailable()
                                            && faceSection.runtimeReady
                }
            }

            Text {
                width: parent.width
                text: qsTr("Face tracking")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: faceSection.faceReady && !faceSection.hasTrack
                         && !EditorState.faceDetecting
                text: qsTr("Scan this clip once, then the Funny Face effects will follow the face through it.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                visible: faceSection.faceReady && !EditorState.faceDetecting
                width: parent.width
                text: faceSection.hasTrack ? qsTr("Re-detect faces") : qsTr("Detect faces…")
                variant: faceSection.hasTrack ? "ghost" : "secondary"
                onClicked: EditorState.detectFacesForClip(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }

            ThemedButton {
                visible: faceSection.faceReady && faceSection.hasTrack
                         && !EditorState.faceDetecting
                width: parent.width
                text: qsTr("Clear face track")
                variant: "ghost"
                onClicked: EditorState.clearFaceTrack(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: EditorState.faceDetecting
                text: EditorState.faceDetectStatus
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedProgressBar {
                visible: EditorState.faceDetecting
                width: parent.width
                value: EditorState.faceDetectProgress
            }

            ThemedButton {
                visible: EditorState.faceDetecting
                width: parent.width
                text: qsTr("Cancel")
                variant: "ghost"
                onClicked: EditorState.cancelFaceDetection()
            }

            ThemedButton {
                visible: !faceSection.faceReady
                width: parent.width
                text: faceSection.runtimeReady
                      ? qsTr("Download face detection (about 5 MB)")
                      : qsTr("Install AI engine first")
                variant: "primary"
                onClicked: root.Window.window.openAddonManager(
                    faceSection.runtimeReady ? "face-model" : "onnxruntime")
            }
        }

        Column {
            width: parent.width
            spacing: 10
            visible: root.selectedEffects.length > 0

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Move to a time, set a value, then click the diamond to add a keyframe. With Auto keyframes on, dragging a slider also creates them.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedChip {
                text: qsTr("Auto keyframes")
                selected: EditorState.autoKeyEnabled
                onClicked: EditorState.autoKeyEnabled = !EditorState.autoKeyEnabled
            }
        }

        // Has a CTA now: the copy told the user to go to the
        // Effects library but gave them no way to get there.
        EmptyState {
            width: parent.width
            visible: root.selectedEffects.length === 0
            glyph: Theme.icons.wand
            title: qsTr("No effects yet")
            hint: qsTr("Drag a preset from the Effects library onto this clip, or click a preset card.")
            actionText: qsTr("Browse effects")
            onActionTriggered: root.browseEffectsRequested()
        }

        Repeater {
            model: root.selectedEffects
            delegate: Column {
                id: effectCard
                required property var modelData
                required property int index
                width: root.width
                spacing: 6

                Rectangle {
                    width: parent.width
                    height: effectHeader.implicitHeight + 8
                    radius: Theme.radiusSm
                    color: Theme.panelAccent

                    Row {
                        id: effectHeader
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        Text {
                            text: effectCard.modelData.label
                            color: Theme.panelForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: Font.Medium
                            width: parent.width - 28
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        IconButton {
                            glyph: Theme.icons.x
                            variant: "ghost"
                            buttonSize: 22
                            iconSize: 12
                            tooltip: qsTr("Remove effect")
                            onClicked: EditorState.removeEffect(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           effectCard.index)
                        }
                    }
                }

                Repeater {
                    model: effectCard.modelData.params || []
                    delegate: Column {
                        id: paramRow
                        required property var modelData
                        width: root.width
                        spacing: 4

                        // Booleans have nothing to interpolate, so they keep the
                        // plain switch and stay off the keyframe strip.
                        Row {
                            visible: !!paramRow.modelData.isBoolean
                            width: parent.width
                            spacing: 8
                            Text {
                                width: parent.width - 48
                                elide: Text.ElideRight
                                text: paramRow.modelData.label
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                width: 40
                                horizontalAlignment: Text.AlignRight
                                text: paramRow.modelData.value ? qsTr("On") : qsTr("Off")
                                color: Theme.panelForeground
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.fontSizeXs
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        ThemedSwitch {
                            visible: !!paramRow.modelData.isBoolean
                            checked: !!paramRow.modelData.value
                            onToggled: EditorState.setEffectParam(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           effectCard.index, paramRow.modelData.key, checked ? 1 : 0)
                        }

                        PropertyKeyframeRow {
                            visible: !paramRow.modelData.isBoolean
                            width: parent.width
                            // `def` is the param's static value, which the row falls
                            // back to whenever the track holds no keys.
                            propDef: ({
                                key: paramRow.modelData.prop,
                                label: paramRow.modelData.label,
                                def: paramRow.modelData.value,
                                decimals: Math.abs(paramRow.modelData.max
                                                   - paramRow.modelData.min) >= 10 ? 1 : 2
                            })
                            keyframeList: (paramRow.modelData.keyframes
                                           && paramRow.modelData.keyframes.points) || []
                            useSlider: true
                            sliderFrom: paramRow.modelData.min
                            sliderTo: paramRow.modelData.max
                        }
                    }
                }
            }
        }
    }
}

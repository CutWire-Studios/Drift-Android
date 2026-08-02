import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""

    height: speedColumn.height
    implicitHeight: speedColumn.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    Column {
        id: speedColumn
        width: root.width
        spacing: Theme.spacingXl

        EmptyState {
            visible: root.clipKind !== "video" && root.clipKind !== "audio"
            width: parent.width
            compact: true
            glyph: Theme.icons.gauge
            title: qsTr("Not available")
            hint: qsTr("Speed applies to video and audio clips.")
        }

        Text {
            visible: root.clipKind === "video" || root.clipKind === "audio"
            text: qsTr("Playback speed")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        // A ramp supersedes the constant rate outright, so the controls below
        // are meaningless while one is attached.
        property bool hasSpeedCurve: {
            void root.clipDataRevision
            return !!root.clipData.hasSpeedCurve
        }

        Row {
            width: parent.width
            spacing: 6
            visible: root.clipKind === "video" || root.clipKind === "audio"

            ThemedButton {
                text: qsTr("Custom speed…")
                variant: "secondary"
                onClicked: root.Window.window.openSpeedCurve(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                visible: speedColumn.hasSpeedCurve
                text: qsTr("Custom speed active — remove")
                selected: true
                onClicked: EditorState.clearClipSpeedCurve(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
        }

            Row {
                width: parent.width
                spacing: 6
                visible: root.clipKind === "video" || root.clipKind === "audio"
                Repeater {
                    model: [
                        { label: "0.25×", value: 0.25 },
                        { label: "0.5×", value: 0.5 },
                        { label: "1×", value: 1.0 },
                        { label: "2×", value: 2.0 },
                        { label: "4×", value: 4.0 }
                    ]
                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.label
                        enabled: !speedColumn.hasSpeedCurve
                        selected: !speedColumn.hasSpeedCurve
                                  && Math.abs((root.clipData.speed || 1) - modelData.value) < 0.01
                        onClicked: EditorState.setClipSpeed(
                                       EditorState.selectedTrack, EditorState.selectedClip,
                                       modelData.value)
                    }
                }
            }

        ThemedSlider {
            id: speedSlider
            visible: root.clipKind === "video" || root.clipKind === "audio"
            enabled: !speedColumn.hasSpeedCurve
            width: parent.width
            from: 0.25
            to: 4.0
            stepSize: 0.05
            value: root.clipData.speed || 1.0
            onMoved: EditorState.previewSetClipSpeed(
                         EditorState.selectedTrack, EditorState.selectedClip, value)
            onPressedChanged: {
                if (pressed) {
                    EditorState.beginPreviewDrag(qsTr("Speed changed"))
                } else {
                    EditorState.commitPreviewDrag()
                    value = Qt.binding(() => root.clipData.speed || 1.0)
                }
            }
        }

        Text {
            visible: root.clipKind === "video" || root.clipKind === "audio"
            text: (speedColumn.hasSpeedCurve
                   ? qsTr("Custom speed")
                   : (root.clipData.speed || 1).toFixed(2) + "×")
                    + (root.clipData.reverse ? qsTr(" (reversed)") : "")
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        ThemedChip {
            visible: root.clipKind === "video" || root.clipKind === "audio"
            text: qsTr("Reverse")
            selected: {
                void root.clipDataRevision
                return !!root.clipData.reverse
            }
            onClicked: EditorState.setClipReverse(
                           EditorState.selectedTrack, EditorState.selectedClip,
                           !root.clipData.reverse)
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.panelBorder
            opacity: 0.5
        }

        Text {
            text: root.clipKind === "audio" ? "Fade in / out (volume)" : "Fade in / out"
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        property real fadeMax: Math.max(0.1, root.clipData.duration || 1)

        Row {
            width: parent.width
            spacing: 6

            ThemedButton {
                text: qsTr("Fade in")
                variant: "secondary"
                onClicked: EditorState.setClipFade(
                               EditorState.selectedTrack, EditorState.selectedClip,
                               0.5, root.clipData.fadeOut || 0)
            }
            ThemedButton {
                text: qsTr("Fade out")
                variant: "secondary"
                onClicked: EditorState.setClipFade(
                               EditorState.selectedTrack, EditorState.selectedClip,
                               root.clipData.fadeIn || 0, 0.5)
            }
            ThemedButton {
                text: qsTr("Clear")
                variant: "ghost"
                enabled: (root.clipData.fadeIn || 0) > 0 || (root.clipData.fadeOut || 0) > 0
                onClicked: EditorState.setClipFade(
                               EditorState.selectedTrack, EditorState.selectedClip, 0, 0)
            }
        }

        Text {
            text: "Fade in: " + (root.clipData.fadeIn || 0).toFixed(2) + "s"
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedSlider {
            id: fadeInSlider
            width: parent.width
            from: 0
            to: parent.fadeMax
            stepSize: 0.05
            value: root.clipData.fadeIn || 0
            onMoved: EditorState.previewSetClipFade(
                         EditorState.selectedTrack, EditorState.selectedClip,
                         value, root.clipData.fadeOut || 0)
            onPressedChanged: {
                if (pressed) {
                    EditorState.beginPreviewDrag(qsTr("Adjust fade"))
                } else {
                    EditorState.commitPreviewDrag()
                    value = Qt.binding(() => root.clipData.fadeIn || 0)
                }
            }
        }

        Text {
            text: "Fade out: " + (root.clipData.fadeOut || 0).toFixed(2) + "s"
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedSlider {
            id: fadeOutSlider
            width: parent.width
            from: 0
            to: parent.fadeMax
            stepSize: 0.05
            value: root.clipData.fadeOut || 0
            onMoved: EditorState.previewSetClipFade(
                         EditorState.selectedTrack, EditorState.selectedClip,
                         root.clipData.fadeIn || 0, value)
            onPressedChanged: {
                if (pressed) {
                    EditorState.beginPreviewDrag(qsTr("Adjust fade"))
                } else {
                    EditorState.commitPreviewDrag()
                    value = Qt.binding(() => root.clipData.fadeOut || 0)
                }
            }
        }

        Text {
            text: qsTr("Fade style")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedComboBox {
            id: fadeCurveCombo
            width: parent.width
            model: [qsTr("Linear"), qsTr("Smooth"), qsTr("Natural")]
            readonly property var curveIds: ["linear", "smooth", "equalPower"]
            currentIndex: Math.max(0, curveIds.indexOf(root.clipData.fadeCurve || "smooth"))
            onActivated: (index) => EditorState.setClipFadeCurve(
                             EditorState.selectedTrack, EditorState.selectedClip,
                             curveIds[index])
        }
    }
}

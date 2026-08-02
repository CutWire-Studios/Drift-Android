import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift
import ".."

// Transport toolbar: timecode readout, play/pause, fit/fill toggle, preview
// quality, guides and fullscreen. The three groups were independently anchored
// and overlapped at narrow preview widths. A RowLayout keeps Play centred
// while the side groups compress instead of colliding.
Item {
    id: toolbar

    // Owning PreviewPanel (playing, projectFps, currentSeconds, durationSeconds,
    // previewFullscreen, formatTimecode, fullscreenRequested) and its viewport
    // (fitMode).
    property var panel
    property var previewViewport

    width: parent.width
    height: Theme.previewToolbarPaddingTop + Theme.previewToolbarPaddingBottom
            + Theme.iconButtonSize

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing2xl + Theme.spacingSm
        anchors.rightMargin: Theme.spacing2xl + Theme.spacingSm
        spacing: Theme.spacingLg

    Row {
        Layout.alignment: Qt.AlignVCenter
        spacing: 0

        HoverHandler { id: timecodeHover }

        ThemedToolTip {
            text: qsTr("Current time / total · %1 frames per second").arg(toolbar.panel.projectFps)
            visible: timecodeHover.hovered
        }

        Text {
            text: toolbar.panel.formatTimecode(toolbar.panel.currentSeconds)
            color: Theme.primary
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: " / "
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: toolbar.panel.formatTimecode(toolbar.panel.durationSeconds)
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // Spacers on both sides keep Play optically centred while still
    // letting the whole row shrink instead of overlap.
    Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

    IconButton {
        Layout.alignment: Qt.AlignVCenter
        glyph: toolbar.panel.playing ? Theme.icons.pause : Theme.icons.play
        variant: "text"
        tooltip: toolbar.panel.playing ? qsTr("Pause") : qsTr("Play")
        onClicked: EditorState.togglePlayback()
    }

    Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

    Row {
        Layout.alignment: Qt.AlignVCenter
        spacing: Theme.spacingLg + Theme.spacingXs

        // Was plain clickable text that read as a label, with no hover,
        // no pressed state and no explanation of the two modes.
        ThemedToggleButton {
            anchors.verticalCenter: parent.verticalCenter
            text: toolbar.previewViewport.fitMode ? qsTr("Fit") : qsTr("Fill")
            checked: !toolbar.previewViewport.fitMode
            tooltip: toolbar.previewViewport.fitMode
                     ? qsTr("Fit: show the whole picture. Click for Fill.")
                     : qsTr("Fill: zoom in to cover the preview area. Click for Fit.")
            onClicked: toolbar.previewViewport.fitMode = !toolbar.previewViewport.fitMode
        }

        Rectangle {
            width: Theme.borderWidth
            height: Theme.iconSizeBase
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        ThemedComboBox {
            id: qualityCombo
            anchors.verticalCenter: parent.verticalCenter
            implicitHeight: Theme.controlHeightSm
            width: implicitContentWidth + leftPadding + rightPadding
            leftPadding: Theme.spacingMd
            rightPadding: Theme.spacing2xl
            font.pixelSize: Theme.fontSizeXs
            // Display is capitalised; the engine stores the lowercase value.
            readonly property var values: ["full", "half", "quarter"]
            model: [qsTr("Full"), qsTr("Half"), qsTr("Quarter")]
            tooltip: qsTr("Preview quality — lower is smoother while editing")
            currentIndex: Math.max(0, values.indexOf(EditorState.playback.previewQuality))
            onActivated: EditorState.playback.previewQuality = values[currentIndex]
        }

        ThemedComboBox {
            id: playbackModeCombo
            anchors.verticalCenter: parent.verticalCenter
            implicitHeight: Theme.controlHeightSm
            width: implicitContentWidth + leftPadding + rightPadding
            leftPadding: Theme.spacingMd
            rightPadding: Theme.spacing2xl
            font.pixelSize: Theme.fontSizeXs
            readonly property var values: ["fast", "quality"]
            model: [qsTr("Fast"), qsTr("Quality")]
            tooltip: qsTr("Playback mode\nFast: plays in real time with audio, skipping frames it "
                          + "cannot render in time — use this for general playback.\n"
                          + "Quality: renders and shows every single frame, silently and slower "
                          + "than real time — use this to check complex transitions frame by frame.")
            currentIndex: Math.max(0, values.indexOf(EditorState.playback.playbackMode))
            onActivated: EditorState.playback.playbackMode = values[currentIndex]
        }

        Rectangle {
            width: Theme.borderWidth
            height: Theme.iconSizeBase
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        IconButton {
            glyph: Theme.icons.grid
            variant: "text"
            tooltip: qsTr("Toggle guides")
            anchors.verticalCenter: parent.verticalCenter
            active: EditorState.guidesEnabled
            onClicked: EditorState.guidesEnabled = !EditorState.guidesEnabled
        }

        IconButton {
            glyph: toolbar.panel.previewFullscreen ? Theme.icons.minimize : Theme.icons.maximize
            variant: "text"
            tooltip: toolbar.panel.previewFullscreen
                     ? qsTr("Exit fullscreen preview (Esc)")
                     : qsTr("Fullscreen preview")
            anchors.verticalCenter: parent.verticalCenter
            active: toolbar.panel.previewFullscreen
            // The window and the surrounding panels belong to Main, so the
            // toggle is requested rather than performed here.
            onClicked: toolbar.panel.fullscreenRequested()
        }
    }
    }
}

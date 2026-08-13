import QtQuick
import QtQuick.Controls.Basic
import Drift

// Live progress for background Whisper subtitle generation. Opens when generation
// starts and closes when it ends; the reject button cancels the run.
ThemedDialog {
    // Back must not reach this one: closing it abandons work already running.
    backDismissable: false
    id: root

    title: qsTr("Generating subtitles")
    preferredWidth: 360
    showAccept: false
    rejectText: qsTr("Cancel")
    // Cancelling throws away a multi-minute run, so it is styled as destructive.
    // The other progress dialogs say "Close" and are genuinely non-destructive.
    rejectVariant: "destructive"
    // Escape now cancels; it used to be inert, leaving no keyboard way to stop a
    // run that can take several minutes.
    closePolicy: Popup.CloseOnEscape

    onRejected: EditorState.cancelSubtitleGeneration()

    Connections {
        target: EditorState
        function onSubtitleGeneratingChanged() {
            if (EditorState.subtitleGenerating)
                root.open()
            else
                root.close()
        }
    }

    contentItem: Column {
        spacing: Theme.spacing2xl - Theme.spacingXs
        width: parent ? parent.width : 328

        LabelledProgressRing {
            width: parent.width
            value: EditorState.subtitleGenProgress
            // Whisper reports nothing until the first chunk lands.
            indeterminate: EditorState.subtitleGenProgress <= 0
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            tone: "default"
            size: "sm"
            wrapMode: Text.WordWrap
            text: EditorState.subtitleGenStatus.length > 0
                  ? EditorState.subtitleGenStatus
                  : qsTr("Working…")
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            size: "xs"
            wrapMode: Text.WordWrap
            text: qsTr("This can take a few minutes on longer clips.")
        }
    }
}

import QtQuick
import QtQuick.Controls.Basic
import Drift

// Shows live progress for a background export. Closable while the export keeps
// running; EditorHeader reopens it via the circular-progress badge next to the
// Export button when it's been dismissed mid-export. Cancel stops the encoder.
ThemedDialog {
    id: root

    title: EditorState.exportInProgress ? qsTr("Exporting video") : qsTr("Export")
    preferredWidth: Theme.dialogWidthSm
    // Cancel export is the primary destructive action while busy; Close dismisses
    // the dialog without stopping the job (badge in the header reopens it).
    showAccept: EditorState.exportInProgress
    acceptText: qsTr("Cancel export")
    acceptVariant: "destructive"
    acceptOnReturn: false
    rejectText: qsTr("Close")
    rejectVariant: "secondary"

    function openDialog() {
        open()
    }

    onAccepted: {
        if (EditorState.exportInProgress)
            EditorState.cancelExport()
    }

    contentItem: Column {
        spacing: Theme.spacing2xl
        width: parent ? parent.width : 308

        LabelledProgressRing {
            width: parent.width
            value: EditorState.exportProgress
            indeterminate: EditorState.exportInProgress && EditorState.exportProgress <= 0
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            size: "sm"
            wrapMode: Text.WordWrap
            text: EditorState.exportInProgress
                  ? qsTr("Rendering your video. Close to keep editing, or cancel to stop.")
                  : qsTr("Export finished.")
        }
    }
}

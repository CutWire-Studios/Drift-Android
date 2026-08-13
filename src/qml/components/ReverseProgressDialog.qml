import QtQuick
import QtQuick.Controls.Basic
import Drift

// Confirm-then-progress for reversing a video clip. Reversing works without a render, but only by
// asking the decoder for an ever-earlier frame — a keyframe seek and a GOP re-decode per frame.
// Rendering a reversed copy first is what makes it play back smoothly, and it is slow enough to be
// worth asking about.
//
// Lives at the window root rather than in the inspector so a selection change mid-render cannot
// take the progress and its Cancel button away.
//
// The footer is custom for the same reason UnsavedChangesDialog's is: confirming has to leave the
// dialog open so it can become the progress readout, and Dialog.accept() closes.
ThemedDialog {
    // Back must not reach this one: closing it abandons work already running.
    backDismissable: false
    id: root

    // The clip the confirm step is about. Once the render starts it no longer depends on the
    // selection, so nothing here is re-read after that.
    property int pendingTrack: -1
    property int pendingClip: -1
    property real pendingSeconds: 0

    readonly property bool rendering: EditorState.reverseRendering

    title: rendering ? qsTr("Reversing clip") : qsTr("Reverse clip")
    preferredWidth: 380
    showFooter: false
    showAccept: false
    showReject: false
    acceptOnReturn: false
    // A render must not be dismissed by a stray click outside; Escape still cancels it.
    closePolicy: rendering ? Popup.CloseOnEscape
                           : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    onClosed: {
        if (root.rendering)
            EditorState.cancelReverseRender()
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.visible && !root.rendering
        onActivated: root.confirm()
    }

    function confirm() {
        // Leaves the dialog open: applyClipReverse flips reverseRendering on, which swaps the
        // content and footer below over to the progress readout.
        EditorState.applyClipReverse(root.pendingTrack, root.pendingClip)
        if (!EditorState.reverseRendering)
            root.close()
    }

    Connections {
        target: EditorState

        function onReverseConfirmRequested(trackIndex, clipIndex, seconds) {
            root.pendingTrack = trackIndex
            root.pendingClip = clipIndex
            root.pendingSeconds = seconds
            root.open()
        }

        function onReverseRenderingChanged() {
            if (!EditorState.reverseRendering)
                root.close()
        }
    }

    contentItem: Column {
        spacing: Theme.spacing2xl
        width: parent ? parent.width : 348

        LabelledProgressRing {
            width: parent.width
            visible: root.rendering
            value: EditorState.reverseRenderProgress
            indeterminate: EditorState.reverseRenderProgress <= 0
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: root.rendering ? Text.AlignHCenter : Text.AlignLeft
            tone: "default"
            size: "sm"
            wrapMode: Text.WordWrap
            text: root.rendering
                  ? (EditorState.reverseRenderStatus.length > 0
                     ? EditorState.reverseRenderStatus
                     : qsTr("Working…"))
                  : qsTr("Drift will render a reversed copy of this clip so it plays back "
                         + "smoothly. You can keep editing while it runs.")
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: root.rendering ? Text.AlignHCenter : Text.AlignLeft
            size: "xs"
            wrapMode: Text.WordWrap
            text: root.rendering
                  ? qsTr("This can take a few minutes on longer clips.")
                  : qsTr("About %1 of video to render.").arg(
                        root.pendingSeconds >= 60
                        ? qsTr("%1 min").arg((root.pendingSeconds / 60).toFixed(1))
                        : qsTr("%1 s").arg(root.pendingSeconds.toFixed(1)))
        }

        Row {
            anchors.right: parent.right
            spacing: Theme.spacingLg

            ThemedButton {
                text: qsTr("Cancel")
                // Cancelling a render throws away work that can take minutes.
                variant: root.rendering ? "destructive" : "secondary"
                onClicked: root.close()
            }

            ThemedButton {
                id: reverseButton
                visible: !root.rendering
                text: qsTr("Reverse")
                variant: "primary"
                onClicked: root.confirm()
            }
        }
    }
}

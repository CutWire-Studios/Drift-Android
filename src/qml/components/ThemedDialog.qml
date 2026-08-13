import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

// Themed modal dialog chrome. Put page content in `contentItem`; use footer buttons
// via acceptText / rejectText (or set showFooter: false and supply your own footer).
Dialog {
    id: root

    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property bool showFooter: true
    property bool showAccept: true
    property bool showReject: true
    property string acceptVariant: "primary"
    property string rejectVariant: "secondary"
    // Natural width; clamped against the window so the dialog always fits.
    property real preferredWidth: Theme.dialogWidthMd
    // Set false for dialogs where Enter must not commit (destructive confirms).
    property bool acceptOnReturn: true
    // Set false for dialogs the system Back key must not dismiss: the recovery
    // prompt, which has no neutral answer, and the progress dialogs, where closing
    // cancels the work behind them.
    property bool backDismissable: true

    modal: true
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.NoButton
    padding: Theme.spacing2xl

    width: Math.min(preferredWidth,
                    (Overlay.overlay ? Overlay.overlay.width : preferredWidth) - Theme.dialogMargin)

    // Height was never clamped, only width. A tall dialog centered in the overlay
    // therefore overflowed both edges, and since the footer is the last thing laid
    // out, its buttons went off-screen with no way to scroll to them — Cancel was
    // reachable only by Escape. Clamping shrinks the content area instead, so the
    // title and the footer buttons always stay on screen.
    // The clamp was against the raw window, which on an edge-to-edge phone includes
    // the status bar and the gesture area. A centred dialog that used the full
    // height therefore put its title under the notch and its footer under the nav
    // bar. Doubled because the dialog is centred: it eats the larger inset at both
    // ends symmetrically.
    readonly property real safeV: Overlay.overlay
        ? 2 * Math.max(Overlay.overlay.SafeArea.margins.top,
                       Overlay.overlay.SafeArea.margins.bottom)
        : 0

    height: Math.min(implicitHeight,
                     (Overlay.overlay ? Overlay.overlay.height : implicitHeight)
                     - Theme.dialogMargin - safeV)

    // Room a contentItem may occupy before the dialog would be clamped. Content
    // that can grow without bound (long option lists, disclosure sections) should
    // put itself in a Flickable capped at this, so it scrolls rather than crops.
    readonly property real availableContentHeight: {
        const overlayHeight = Overlay.overlay ? Overlay.overlay.height : 720
        return Math.max(120, overlayHeight - Theme.dialogMargin - root.safeV
                             - dialogHeader.implicitHeight - dialogFooter.implicitHeight
                             - topPadding - bottomPadding)
    }

    // Enter/Return commits accept. A Dialog is a Popup, not an Item, so `Keys`
    // cannot attach to it (it silently warned and never fired); a Shortcut is a
    // QObject and works regardless of which control holds focus. Gated on
    // `visible` so only the open dialog's shortcut is live — no cross-dialog
    // ambiguity when several are instantiated. Escape is handled by Popup's
    // closePolicy.
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.visible && root.acceptOnReturn && root.showAccept && acceptButton.enabled
        onActivated: root.accept()
    }

    // Give the dialog focus on open so the key handlers above are live and the
    // default action is visibly selected.
    // Android's Back key is not Escape, so closePolicy never saw it. Registering
    // here is what makes Back dismiss the dialog on top instead of walking out of
    // the editor and leaving it on screen — the same contract ThemedContextMenu
    // already uses. No-op on desktop, which defines no pushMenu.
    property var _backHost: null

    onOpened: {
        if (showAccept)
            acceptButton.forceActiveFocus()
        else
            root.forceActiveFocus()
    }

    // Via Connections, not `onOpened:`/`onClosed:` handlers. A derived dialog that
    // declares its own handler for the same signal replaces the one written here —
    // AddonManagerDialog and AddonStartupDialog both do — which silently unhooked
    // the registration and left exactly those dialogs undismissable by Back.
    Connections {
        target: root

        function onOpened() {
            root._backHost = (Theme.touchUi && root.backDismissable && root.parent)
                             ? root.parent.Window.window : null
            if (root._backHost && root._backHost.pushMenu)
                root._backHost.pushMenu(root)
        }

        function onClosed() {
            if (root._backHost && root._backHost.popMenu)
                root._backHost.popMenu(root)
            root._backHost = null
        }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 0.96
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 1.0
                to: 0.96
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
        }
    }

    background: Rectangle {
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder
        radius: Theme.radiusMd

        // Attached here rather than on the Dialog: a Dialog is a Popup, not an Item,
        // and Accessible only attaches to Item or Action — the same constraint the
        // Return shortcut above works around.
        Accessible.role: Accessible.Dialog
        Accessible.name: root.title
    }

    header: Item {
        id: dialogHeader
        implicitHeight: root.title.length > 0 ? 44 : 0
        width: root.width
        visible: root.title.length > 0

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing2xl
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing2xl
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.borderWidth
            color: Theme.panelBorder
        }
    }

    footer: Item {
        id: dialogFooter
        visible: root.showFooter
        implicitHeight: visible ? 56 : 0
        width: root.width

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Theme.borderWidth
            color: Theme.panelBorder
            visible: root.showFooter
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing2xl
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingLg

            ThemedButton {
                visible: root.showReject
                text: root.rejectText
                variant: root.rejectVariant
                onClicked: root.reject()
            }

            ThemedButton {
                id: acceptButton
                visible: root.showAccept
                text: root.acceptText
                variant: root.acceptVariant
                onClicked: root.accept()
            }
        }
    }
}

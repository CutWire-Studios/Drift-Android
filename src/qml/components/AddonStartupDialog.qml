import QtQuick
import QtQuick.Controls.Basic
import Drift

// Essential packs + update nudge. Never opens by itself — the Extras icon in the header pulses
// while there is something to show, and the user opens this from there (same pattern as the
// app-update badge). Both sections honour a "don't remind me" preference.
ThemedDialog {
    id: root

    property var essentialAddons: []
    property var updateAddons: []
    property var transfers: ({})
    property bool dontRemindEssential: false
    property bool dontRemindUpdates: false
    // After "Later", the header opens the full Extras manager instead of this dialog
    // again — the shockwave still pulses until the underlying need is cleared.
    property bool attentionAcknowledged: false

    readonly property bool showEssential: essentialAddons.length > 0
    readonly property bool showUpdates: updateAddons.length > 0
    // Drives the header shockwave; true while either section has something to offer.
    readonly property bool needsAttention: showEssential || showUpdates
    readonly property var actionIds: {
        var ids = []
        var i
        for (i = 0; i < essentialAddons.length; ++i)
            ids.push(essentialAddons[i].id)
        for (i = 0; i < updateAddons.length; ++i)
            ids.push(updateAddons[i].id)
        return ids
    }

    title: {
        if (showEssential && showUpdates)
            return qsTr("Extra packs")
        if (showUpdates)
            return qsTr("Pack updates available")
        return qsTr("Recommended packs")
    }
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    acceptOnReturn: false

    onNeedsAttentionChanged: {
        if (!needsAttention)
            attentionAcknowledged = false
    }

    // Rebuilds the lists that feed needsAttention. Safe to call on every catalog churn;
    // does not open the dialog.
    function refreshAttention() {
        if (Addons.refreshing)
            return
        // Leave in-flight install rows alone while the dialog is already open.
        if (visible && installing)
            return

        essentialAddons = Addons.remindEssential ? Addons.missingEssentialAddons() : []
        updateAddons = Addons.remindUpdates ? Addons.updatableAddons() : []
    }

    // Opened from the header when needsAttention is true and the user has not yet
    // waved this dialog away. Returns false so the caller can fall through to the
    // full Extras manager.
    function openForAttention() {
        refreshAttention()
        if (!needsAttention || attentionAcknowledged)
            return false
        dontRemindEssential = false
        dontRemindUpdates = false
        transfers = ({})
        open()
        return true
    }

    function persistReminders() {
        if (dontRemindEssential && showEssential)
            Addons.remindEssential = false
        if (dontRemindUpdates && showUpdates)
            Addons.remindUpdates = false
    }

    // True from the moment installs are kicked off until every one has reported an
    // outcome. The dialog used to close() immediately here, which orphaned its own
    // progress rows below and left hundreds of MB downloading with no indication
    // that anything was happening — or that anything had failed.
    property bool installing: false
    property int pendingCount: 0

    function installListed() {
        if (installing || actionIds.length === 0)
            return
        persistReminders()
        root.installing = true
        root.pendingCount = actionIds.length
        for (var i = 0; i < actionIds.length; ++i)
            Addons.install(actionIds[i])
    }

    // Counts a transfer out whichever way it ended. Failures are surfaced as toasts
    // from Main.qml, so this only has to decide when the dialog is done.
    function noteTransferSettled(id) {
        if (!root.installing || root.actionIds.indexOf(id) < 0)
            return
        root.pendingCount = Math.max(0, root.pendingCount - 1)
        if (root.pendingCount === 0) {
            root.installing = false
            root.close()
        }
    }

    function dismiss() {
        persistReminders()
        attentionAcknowledged = true
        close()
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.visible && !root.installing
        onActivated: root.installListed()
    }

    Connections {
        target: Addons
        function onProgressChanged(id, fraction, phase) {
            var next = root.transfers
            next[id] = { fraction: fraction, phase: phase }
            root.transfers = next
        }
        function onTransferSucceeded(id) { root.noteTransferSettled(id) }
        function onTransferFailed(id, reason) { root.noteTransferSettled(id) }
    }

    // One row per pack plus two checkboxes and a button row grows past the window on
    // a short display, which used to push the button row off-screen entirely.
    contentItem: Flickable {
        id: contentFlick
        width: parent ? parent.width : Theme.dialogWidthMd
        implicitHeight: Math.min(startupColumn.height, root.availableContentHeight)
        contentWidth: width
        contentHeight: startupColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        ScrollBar.vertical: AppScrollBar {
            policy: contentFlick.contentHeight > contentFlick.height
                    ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
        }

        Column {
            id: startupColumn
            spacing: Theme.spacingLg
            width: contentFlick.width

            ThemedLabel {
                width: parent.width
                size: "sm"
                visible: root.showEssential
                text: qsTr("Install the essential packs for effects, transitions, and audio. "
                           + "You can keep using Drift without them — installing unlocks updates "
                           + "when they improve.")
            }

            Repeater {
                model: root.essentialAddons

                Rectangle {
                    id: essentialRow

                    required property var modelData

                    readonly property var transfer: root.transfers[modelData.id]

                    width: parent ? parent.width : 0
                    height: essentialBody.implicitHeight + Theme.spacingXl
                    radius: Theme.radiusMd
                    color: Theme.panelSecondaryBg
                    border.width: Theme.borderWidth
                    border.color: Theme.panelSecondaryBorder

                    Column {
                        id: essentialBody
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Theme.spacingXl
                        anchors.rightMargin: Theme.spacingXl
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingXs

                        ThemedLabel {
                            width: parent.width
                            text: essentialRow.modelData.name
                            tone: "default"
                            size: "sm"
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }

                        ThemedLabel {
                            width: parent.width
                            text: essentialRow.transfer
                                  ? qsTr("%1… %2%").arg(essentialRow.transfer.phase)
                                                    .arg(Math.round(essentialRow.transfer.fraction * 100))
                                  : (essentialRow.modelData.description || essentialRow.modelData.id)
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            ThemedCheckBox {
                width: parent.width
                visible: root.showEssential
                checked: root.dontRemindEssential
                text: qsTr("Don't remind me of essential addons")
                onToggled: root.dontRemindEssential = checked
            }

            Rectangle {
                width: parent.width
                height: Theme.borderWidth
                color: Theme.panelBorder
                visible: root.showEssential && root.showUpdates
            }

            ThemedLabel {
                width: parent.width
                size: "sm"
                visible: root.showUpdates
                text: qsTr("Updates are available for packs you already have installed.")
            }

            Repeater {
                model: root.updateAddons

                Rectangle {
                    id: updateRow

                    required property var modelData

                    readonly property var transfer: root.transfers[modelData.id]

                    width: parent ? parent.width : 0
                    height: updateBody.implicitHeight + Theme.spacingXl
                    radius: Theme.radiusMd
                    color: Theme.panelSecondaryBg
                    border.width: Theme.borderWidth
                    border.color: Theme.panelSecondaryBorder

                    Column {
                        id: updateBody
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Theme.spacingXl
                        anchors.rightMargin: Theme.spacingXl
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingXs

                        ThemedLabel {
                            width: parent.width
                            text: updateRow.modelData.name
                            tone: "default"
                            size: "sm"
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }

                        ThemedLabel {
                            width: parent.width
                            text: {
                                if (updateRow.transfer)
                                    return qsTr("%1… %2%").arg(updateRow.transfer.phase)
                                                          .arg(Math.round(updateRow.transfer.fraction * 100))
                                return qsTr("%1 → %2").arg(updateRow.modelData.installedVersion)
                                                      .arg(updateRow.modelData.version)
                            }
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            ThemedCheckBox {
                width: parent.width
                visible: root.showUpdates
                checked: root.dontRemindUpdates
                text: qsTr("Don't remind me of future addon updates")
                onToggled: root.dontRemindUpdates = checked
            }

            Item {
                width: parent.width
                height: installButton.height

                ThemedButton {
                    anchors.left: parent.left
                    variant: "ghost"
                    // Downloads keep running in the background either way; this only
                    // stops watching them.
                    text: root.installing ? qsTr("Hide") : qsTr("Later")
                    onClicked: root.dismiss()
                }

                ThemedButton {
                    id: installButton
                    anchors.right: parent.right
                    variant: "primary"
                    glyph: Theme.icons.download
                    enabled: !root.installing
                    text: {
                        if (root.installing)
                            return qsTr("Installing…")
                        if (root.showEssential && root.showUpdates)
                            return qsTr("Install & update")
                        if (root.showUpdates)
                            return root.updateAddons.length === 1 ? qsTr("Update") : qsTr("Update all")
                        return root.essentialAddons.length === 1 ? qsTr("Install") : qsTr("Install all")
                    }
                    onClicked: root.installListed()
                }
            }
        }
    }

    onOpened: installButton.forceActiveFocus()
}

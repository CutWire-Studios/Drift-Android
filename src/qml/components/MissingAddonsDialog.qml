import QtQuick
import QtQuick.Controls.Basic
import Drift

// Shown after opening a project whose bundle names addons this machine does not have. The project
// is already loaded — the effects those addons provide simply render as no-ops until installed.
ThemedDialog {
    id: root

    // id / name / version / kinds, as recorded when the project was saved.
    property var addons: []
    // id -> { fraction, phase }, mirroring AddonManagerDialog.
    property var transfers: ({})

    title: qsTr("Extra packs needed")
    acceptText: qsTr("Install all")
    rejectText: qsTr("Skip")
    preferredWidth: Theme.dialogWidthMd

    function openFor(list) {
        addons = list
        transfers = ({})
        // A stale index rejects installs with expired download tickets.
        Addons.refresh(false)
        open()
    }

    onAccepted: {
        for (var i = 0; i < addons.length; ++i)
            Addons.install(addons[i].id)
    }

    Connections {
        target: Addons
        function onProgressChanged(id, fraction, phase) {
            var next = root.transfers
            next[id] = { fraction: fraction, phase: phase }
            root.transfers = next
        }
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 400

        ThemedLabel {
            width: parent.width
            size: "sm"
            text: qsTr("This project was saved with extra packs you don't have. It has opened, but "
                       + "anything they provide will not show until they are installed.")
        }

        Repeater {
            model: root.addons

            Rectangle {
                id: row

                required property var modelData

                readonly property var transfer: root.transfers[modelData.id]
                readonly property bool installed:
                    Addons.catalog.some(function (a) {
                        return a.id === row.modelData.id && a.installedVersion.length > 0
                    })

                width: parent ? parent.width : 0
                height: rowBody.implicitHeight + Theme.spacingXl
                radius: Theme.radiusMd
                color: Theme.panelSecondaryBg
                border.width: Theme.borderWidth
                border.color: Theme.panelSecondaryBorder

                Column {
                    id: rowBody
                    anchors.left: parent.left
                    anchors.right: rowActions.left
                    anchors.leftMargin: Theme.spacingXl
                    anchors.rightMargin: Theme.spacingLg
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingXs

                    ThemedLabel {
                        width: rowBody.width
                        text: row.modelData.name
                        tone: "default"
                        size: "sm"
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    ThemedLabel {
                        width: rowBody.width
                        text: {
                            if (row.transfer)
                                return qsTr("%1… %2%").arg(row.transfer.phase)
                                                      .arg(Math.round(row.transfer.fraction * 100))
                            var parts = [row.modelData.id]
                            if (row.modelData.version.length > 0)
                                parts.push(qsTr("used version %1").arg(row.modelData.version))
                            return parts.join(" · ")
                        }
                        elide: Text.ElideRight
                    }
                }

                Row {
                    id: rowActions
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingXl
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingLg

                    CircularProgress {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.transfer !== undefined && !row.installed
                        indeterminate: !row.transfer || row.transfer.fraction <= 0
                        value: row.transfer ? row.transfer.fraction : 0
                    }

                    ThemedButton {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !row.installed && row.transfer === undefined
                        text: qsTr("Install")
                        variant: "primary"
                        onClicked: Addons.install(row.modelData.id)
                    }

                    IconGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.installed
                        glyph: Theme.icons.success
                        iconSize: Theme.iconSizeMd
                        iconColor: Theme.primary
                    }
                }
            }
        }

        ThemedLabel {
            width: parent.width
            size: "xs"
            text: qsTr("Reopen the project once they finish installing.")
        }
    }
}

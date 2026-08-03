import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"

// Slim CapCut-style editor top bar.
Item {
    id: root

    signal backRequested()
    signal exportRequested()
    signal exportProgressRequested()
    signal saveRequested()
    signal packageRequested()
    signal openRequested()
    signal newRequested()
    signal layoutRequested()

    // Status bar / camera cutout. Without it the Back, Undo and Export buttons sat
    // underneath the system bar on every edge-to-edge device.
    readonly property real topInset: SafeArea.margins.top
    readonly property real leftInset: SafeArea.margins.left
    readonly property real rightInset: SafeArea.margins.right

    height: Theme.androidTopBarHeight + topInset
    width: parent ? parent.width : 0

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBackground
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.panelBorder
    }

    Item {
        id: barBody
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset
        height: Theme.androidTopBarHeight

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingXs

            IconButton {
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.chevronLeft
                variant: "text"
                tooltip: qsTr("Back")
                onClicked: root.backRequested()
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(140, implicitWidth)
                text: EditorState.projectName.length > 0
                      ? EditorState.projectName
                      : qsTr("Untitled")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
                elide: Text.ElideRight
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 4
                color: EditorState.hasUnsavedChanges ? Theme.destructive : Theme.constructive
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingXs

            IconButton {
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.undo
                variant: "text"
                tooltip: qsTr("Undo")
                enabled: EditorState.undoAvailable
                onClicked: EditorState.undo()
            }

            IconButton {
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.redo
                variant: "text"
                tooltip: qsTr("Redo")
                enabled: EditorState.redoAvailable
                onClicked: EditorState.redo()
            }

            // Doubles as the way back into a render whose progress sheet was dismissed.
            // Without it a running export was unreachable and uncancellable — there is no
            // second window on a phone to leave it open in.
            Item {
                width: Theme.androidIconButtonSize
                height: Theme.androidIconButtonSize
                anchors.verticalCenter: parent.verticalCenter

                IconButton {
                    anchors.fill: parent
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeLg
                    glyph: Theme.icons.upload
                    variant: "text"
                    visible: !EditorState.exportInProgress
                    tooltip: qsTr("Export")
                    onClicked: root.exportRequested()
                }

                CircularProgress {
                    anchors.centerIn: parent
                    visible: EditorState.exportInProgress
                    size: Theme.iconSizeXl
                    strokeWidth: 3
                    value: EditorState.exportProgress
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: EditorState.exportInProgress
                    onClicked: root.exportProgressRequested()
                }
            }

            IconButton {
                id: moreBtn
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.sliders
                variant: "text"
                tooltip: qsTr("More")
                active: overflowMenu.visible
                onClicked: overflowMenu.visible ? overflowMenu.close() : overflowMenu.open()
            }
        }
    }

    // So the editor's Back handler can dismiss the menu before anything else.
    readonly property bool menuOpen: overflowMenu.visible
    function closeMenu() { overflowMenu.close() }

    Popup {
        id: overflowMenu
        parent: barBody
        width: 220
        padding: Theme.spacingMd
        modal: true
        dim: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.panelBackground
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder
            radius: Theme.radiusMd
        }

        contentItem: Column {
            spacing: 2
            width: parent.width

            component MenuRow: Rectangle {
                id: menuRow
                property alias text: menuLabel.text
                property alias glyph: menuIcon.glyph
                signal triggered()
                width: parent ? parent.width : 0
                height: visible ? Math.max(44, Theme.controlHeight) : 0
                radius: Theme.radiusSm
                color: menuArea.containsMouse ? Theme.accent : "transparent"

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingLg
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingLg

                    IconGlyph {
                        id: menuIcon
                        iconSize: Theme.iconSizeMd
                        iconColor: Theme.foreground
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        id: menuLabel
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: menuArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        overflowMenu.close()
                        menuRow.triggered()
                    }
                }
            }

            MenuRow {
                text: qsTr("Save")
                glyph: Theme.icons.save
                onTriggered: root.saveRequested()
            }
            MenuRow {
                text: qsTr("Shareable copy")
                glyph: Theme.icons.package
                onTriggered: root.packageRequested()
            }
            MenuRow {
                text: qsTr("Open project")
                glyph: Theme.icons.folder
                onTriggered: root.openRequested()
            }
            MenuRow {
                text: qsTr("New project")
                glyph: Theme.icons.plus
                onTriggered: root.newRequested()
            }
            MenuRow {
                text: qsTr("Choose layout")
                glyph: Theme.icons.ratio
                onTriggered: root.layoutRequested()
            }
            MenuRow {
                text: Theme.darkMode ? qsTr("Light mode") : qsTr("Dark mode")
                glyph: Theme.darkMode ? Theme.icons.sun : Theme.icons.moon
                onTriggered: Theme.toggleDarkMode()
            }
            MenuRow {
                text: qsTr("Extras")
                glyph: Theme.icons.package
                onTriggered: Window.window.openExtras()
            }
            MenuRow {
                text: qsTr("Update available")
                glyph: Theme.icons.download
                visible: Updates.updateAvailable
                onTriggered: Window.window.openUpdateDialog()
            }
        }

        // moreBtn's x is relative to the Row it sits in, not to barBody, so deriving the
        // position from it put the menu near the left edge instead of under the button.
        // The button is the last item on the right, so the bar's own right edge is the
        // anchor — and it already accounts for the landscape cutout inset.
        onAboutToShow: {
            x = Math.max(Theme.spacingSm, barBody.width - width - Theme.spacingSm)
            y = barBody.height + 4
        }
    }
}

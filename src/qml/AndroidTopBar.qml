import QtQuick
import QtQuick.Controls.Basic
import Drift
import "components"

// Slim CapCut-style editor top bar.
Item {
    id: root

    signal backRequested()
    signal exportRequested()
    signal saveRequested()
    signal packageRequested()
    signal openRequested()
    signal newRequested()
    signal layoutRequested()

    height: Theme.androidTopBarHeight
    width: parent ? parent.width : 0

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.panelBorder
    }

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

        IconButton {
            buttonSize: Theme.androidIconButtonSize
            iconSize: Theme.iconSizeLg
            glyph: Theme.icons.upload
            variant: "text"
            tooltip: qsTr("Export")
            onClicked: root.exportRequested()
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

    Popup {
        id: overflowMenu
        parent: root
        width: 200
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
                height: 40
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
        }

        onAboutToShow: {
            x = Math.max(8, moreBtn.x + moreBtn.width - width)
            y = moreBtn.y + moreBtn.height + 4
        }
    }
}

import QtQuick
import QtQuick.Controls.Basic
import Drift

// Compact style-pack selector for the properties Text tab: shows the active pack
// and opens a popup grid to switch. Hand-edited styles (empty packId) show as Custom.
Item {
    id: root

    property string packId: ""
    signal packPicked(string packId)

    readonly property var presets: EditorState.textPresets()
    readonly property string displayLabel: {
        if (root.packId.length === 0)
            return qsTr("Custom")
        for (let i = 0; i < root.presets.length; ++i) {
            if (root.presets[i].id === root.packId)
                return root.presets[i].label
        }
        return qsTr("Custom")
    }

    implicitHeight: 56
    implicitWidth: 200

    Keys.onEscapePressed: (event) => {
        if (popup.visible) {
            popup.close()
            event.accepted = true
        }
    }

    Rectangle {
        id: trigger
        anchors.fill: parent
        radius: Theme.radiusSm
        color: Theme.panelAccent
        border.width: (popup.visible || root.activeFocus) ? 1 : 0
        border.color: root.activeFocus ? Theme.primary : Theme.panelSecondaryBorder

        TextStylePackThumb {
            id: triggerThumb
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.top: parent.top
            anchors.topMargin: 6
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            width: Math.round(height * 2.0)
            presetId: root.packId
            selected: false
            hovered: triggerHover.hovered
        }

        Text {
            anchors.left: triggerThumb.right
            anchors.leftMargin: 8
            anchors.right: chevron.left
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: root.displayLabel
            elide: Text.ElideRight
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        IconGlyph {
            id: chevron
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.chevronDown
            iconSize: 12
            iconColor: Theme.mutedForeground
        }

        HoverHandler {
            id: triggerHover
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.forceActiveFocus()
                if (popup.visible)
                    popup.close()
                else
                    popup.open()
            }
        }
    }

    Popup {
        id: popup
        y: root.height + 2
        width: Math.max(root.width, 260)
        padding: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.panelBackground
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder
        }

        contentItem: Flickable {
            id: flick
            clip: true
            implicitWidth: popup.width - popup.padding * 2
            implicitHeight: Math.min(320, packGrid.height)
            contentHeight: packGrid.height
            ScrollBar.vertical: AppScrollBar { }

            Grid {
                id: packGrid
                width: flick.width
                columns: 2
                spacing: 8
                readonly property real cellWidth: (width - spacing * (columns - 1)) / columns

                Repeater {
                    model: root.presets
                    delegate: Column {
                        id: packCard
                        required property var modelData
                        readonly property bool selected: root.packId === modelData.id
                        width: packGrid.cellWidth
                        spacing: 3

                        Text {
                            width: parent.width
                            text: packCard.modelData.label
                            elide: Text.ElideRight
                            color: packCard.selected ? Theme.primary : Theme.panelForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }

                        TextStylePackThumb {
                            width: parent.width
                            height: Math.round(width * 0.48)
                            presetId: packCard.modelData.id
                            selected: packCard.selected
                            hovered: packHover.hovered

                            HoverHandler {
                                id: packHover
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.packPicked(packCard.modelData.id)
                                    popup.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

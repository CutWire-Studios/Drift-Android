import QtQuick
import QtQuick.Controls.Basic
import Drift

// Browsable effect preset picker: category chips + card grid.
// Drag a card onto a timeline clip, or click / tap + to apply to the selection.
Column {
    id: root
    spacing: 0

    readonly property var categories: EditorState.effectCategories()
    property string activeCategory: categories.length > 0 ? categories[0].id : ""

    function applyPreset(effectId) {
        if (EditorState.selectedClip < 0)
            return
        EditorState.addEffect(EditorState.selectedTrack, EditorState.selectedClip, effectId)
    }

    Text {
        id: browserTip
        width: parent.width - 24
        leftPadding: 12
        rightPadding: 12
        topPadding: 8
        bottomPadding: 4
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        text: EditorState.selectedClip >= 0
              ? qsTr("Drag a preset onto a clip, or click to apply to the selection")
              : qsTr("Drag a preset onto a clip in the timeline")
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Flickable {
        id: categoryFlick
        width: parent.width
        height: 34
        contentWidth: categoryRow.width + 24
        clip: true

        Row {
            id: categoryRow
            x: 12
            height: parent.height
            spacing: 6

            Repeater {
                model: root.categories
                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    variant: "secondary"
                    selected: modelData.id === root.activeCategory
                    onClicked: root.activeCategory = modelData.id
                }
            }
        }
    }

    Flickable {
        width: parent.width
        height: Math.max(0, root.height - browserTip.height - categoryFlick.height)
        contentHeight: presetGrid.height + 24
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Grid {
            id: presetGrid
            x: 12
            y: 12
            width: parent.width - 24
            columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
            columnSpacing: Theme.assetCardGap
            rowSpacing: Theme.assetCardGap

            Repeater {
                model: EditorState.effectCatalog()
                delegate: Column {
                    id: presetCard
                    required property var modelData
                    visible: presetCard.modelData.category === root.activeCategory
                    width: visible ? Theme.assetCardWidth : 0
                    spacing: 4
                    opacity: presetDrag.active ? 0.85 : 1

                    readonly property string thumb: presetCard.modelData.thumbnailPath || ""

                    Drag.active: presetDrag.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.keys: ["application/x-drift-effect"]
                    Drag.mimeData: ({ "application/x-drift-effect": presetCard.modelData.id })
                    Drag.hotSpot.x: width / 2
                    Drag.hotSpot.y: Theme.assetCardWidth / 2

                    Rectangle {
                        width: Theme.assetCardWidth
                        height: Theme.assetCardWidth
                        radius: Theme.radiusSm
                        color: cardHover.hovered ? Theme.panelSecondaryBg : Theme.panelAccent
                        border.width: presetDrag.active ? 1 : 0
                        border.color: Theme.primary
                        clip: true

                        HoverHandler { id: cardHover }

                        Image {
                            anchors.fill: parent
                            visible: presetCard.thumb.length > 0
                            source: presetCard.thumb.length > 0
                                    ? EditorState.imageUrl(presetCard.thumb) : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            smooth: true
                        }

                        // Fallback when no thumbnail is present yet.
                        Text {
                            anchors.centerIn: parent
                            visible: presetCard.thumb.length === 0
                            width: parent.width - 12
                            text: presetCard.modelData.label
                            color: Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeCard
                            font.weight: Font.Medium
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            maximumLineCount: 3
                            elide: Text.ElideRight
                        }

                        TapHandler {
                            enabled: !presetDrag.active
                            onTapped: root.applyPreset(presetCard.modelData.id)
                        }

                        DragHandler {
                            id: presetDrag
                            target: null
                            acceptedButtons: Qt.LeftButton
                        }

                        IconButton {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 3
                            glyph: Theme.icons.plus
                            variant: "ghost"
                            buttonSize: 18
                            iconSize: 12
                            tooltip: qsTr("Apply to selected clip")
                            enabled: EditorState.selectedClip >= 0
                            onClicked: root.applyPreset(presetCard.modelData.id)
                        }
                    }

                    Text {
                        width: parent.width
                        text: presetCard.modelData.label
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeCard
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        visible: presetCard.modelData.compositorOnly === true
                        text: qsTr("Built-in")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs - 1
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}

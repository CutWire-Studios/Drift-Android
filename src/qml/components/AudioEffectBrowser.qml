import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

// Browsable audio-effect preset picker: category chips + card grid.
// Drag a card onto a timeline clip, or click / tap + to apply to the selection.
Column {
    id: root
    spacing: 0

    readonly property var categories: EditorState.audioEffectCategories()
    readonly property var catalog: EditorState.audioEffectCatalog()
    property string activeCategory: categories.length > 0 ? categories[0].id : ""

    function applyPreset(effectId) {
        if (EditorState.selectedClip < 0)
            return
        EditorState.addAudioEffect(EditorState.selectedTrack, EditorState.selectedClip, effectId)
    }

    // Catalog comes from the audio-effects addon (or a local audio-effects/ tree).
    EmptyState {
        width: parent.width
        height: visible ? root.height : 0
        visible: root.catalog.length === 0
        glyph: Theme.icons.headphones
        title: qsTr("No audio effects")
        hint: qsTr("Install the Audio Effects pack from Extras to browse presets here.")
        actionText: qsTr("Install audio effects")
        onActionTriggered: root.Window.window.openAddonManager("audio-effects")
    }

    Text {
        id: browserTip
        visible: root.catalog.length > 0
        width: parent.width - 24
        height: visible ? implicitHeight : 0
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
        visible: root.catalog.length > 0
        width: parent.width
        height: visible ? 34 : 0
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
        visible: root.catalog.length > 0
        width: parent.width
        height: visible ? Math.max(0, root.height - browserTip.height - categoryFlick.height) : 0
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
                model: root.catalog
                delegate: Column {
                    id: presetCard
                    required property var modelData
                    visible: presetCard.modelData.category === root.activeCategory
                    width: visible ? Theme.assetCardWidth : 0
                    spacing: 4
                    opacity: presetDrag.active ? 0.85 : 1

                    readonly property string thumb: presetCard.modelData.thumbnailPath || ""
                    readonly property string iconGlyph: presetCard.modelData.icon || "audio-lines"

                    Drag.active: presetDrag.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.keys: ["application/x-drift-audio-effect"]
                    Drag.mimeData: ({ "application/x-drift-audio-effect": presetCard.modelData.id })
                    Drag.hotSpot.x: width / 2
                    Drag.hotSpot.y: Theme.assetCardWidth / 2

                    Rectangle {
                        width: Theme.assetCardWidth
                        height: Theme.assetCardWidth
                        radius: Theme.radiusSm
                        color: cardHover.hovered ? Theme.panelAccent : Theme.panelBackground
                        border.width: presetDrag.active ? 1 : 0
                        border.color: Theme.primary
                        clip: true

                        HoverHandler { id: cardHover }

                        Image {
                            id: presetThumb
                            // Soft-alpha AuraBlur PNGs fade out at the rim; zoom past that
                            // falloff so the card reads as a full-bleed colour field, not a
                            // vignette floating on the panel accent.
                            anchors.fill:parent
                            visible: presetCard.thumb.length > 0 && status === Image.Ready
                            source: presetCard.thumb.length > 0
                                    ? EditorState.imageUrl(presetCard.thumb) : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            smooth: true
                        }

                        // Fallback when no package thumbnail is present yet.
                        IconGlyph {
                            anchors.centerIn: parent
                            visible: presetCard.thumb.length === 0
                                     || presetThumb.status === Image.Error
                            glyph: presetCard.iconGlyph
                            iconSize: 28
                            iconColor: Theme.mutedForeground
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
                }
            }
        }
    }
}

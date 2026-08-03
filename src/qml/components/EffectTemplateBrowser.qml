import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "assets"

// Beat-synced multi-effect presets: category chips + card grid.
Column {
    id: root
    spacing: 0

    readonly property string favoritesId: "__favorites__"
    readonly property var categories: EditorState.effectTemplateCategories()
    readonly property var catalog: EditorState.effectTemplateCatalog()
    property string activeCategory: categories.length > 0 ? categories[0].id : ""
    readonly property string query: search.text.trim().toLowerCase()
    property int favoritesTick: 0

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() {
            root.favoritesTick++
        }
    }

    // Search spans every category — once you have a name, the sectors are in the way.
    readonly property var visibleTemplates: {
        void root.favoritesTick
        const q = root.query
        if (q.length > 0) {
            return root.catalog.filter(function(preset) {
                const label = (preset.label || "").toLowerCase()
                const id = (preset.id || "").toLowerCase()
                return label.indexOf(q) >= 0 || id.indexOf(q) >= 0
            })
        }
        if (root.activeCategory === root.favoritesId) {
            return root.catalog.filter(function(preset) {
                return EditorState.isAssetFavorite("templates", preset.id)
            })
        }
        return root.catalog.filter(function(preset) {
            return preset.category === root.activeCategory
        })
    }

    function applyTemplate(templateId) {
        if (EditorState.selectedClip < 0)
            return
        EditorState.applyEffectTemplate(EditorState.selectedTrack, EditorState.selectedClip, templateId)
    }

    // Without the pack installed the catalog is empty and the grid rendered nothing,
    // unlike the sibling audio and transitions tabs which both offer an install CTA.
    EmptyState {
        width: parent.width
        height: visible ? root.height : 0
        visible: root.catalog.length === 0
        glyph: Theme.icons.wand
        title: qsTr("No effect templates")
        hint: qsTr("Install the Effect Templates pack from Extras to browse presets here.")
        actionText: qsTr("Get extras")
        onActionTriggered: root.Window.window.openAddonManager()
    }

    Column {
        visible: root.catalog.length > 0
        width: parent.width
        height: parent.height
        spacing: 0

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
                  ? qsTr("Click a template to apply music-synced effects to the selection")
                  : qsTr("Select a clip, then click a template to apply")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedTextField {
            id: search
            width: parent.width - 24
            x: 12
            placeholderText: qsTr("Search templates")
            font.family: Theme.fontFamily
        }

        Item {
            width: 1
            height: Theme.spacingMd
        }

        AssetCategoryChips {
            id: categoryChips
            width: parent.width
            categories: root.categories
            activeCategory: root.activeCategory
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => root.activeCategory = categoryId
        }

        Flickable {
            width: parent.width
            height: Math.max(0, parent.height - browserTip.height - search.height - Theme.spacingMd
                             - categoryChips.height)
            contentHeight: Math.max(emptySearchHint.height, presetGrid.height) + 24
            clip: true
            ScrollBar.vertical: AppScrollBar { }

        Text {
        id: emptySearchHint
        x: 12
        y: 12
        width: parent.width - 24
        visible: root.visibleTemplates.length === 0
        text: root.query.length > 0
              ? qsTr("No templates match “%1”.").arg(search.text.trim())
              : (root.activeCategory === root.favoritesId
                 ? qsTr("No favorites yet. Star templates to save them here.")
                 : qsTr("Nothing in this category."))
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        wrapMode: Text.WordWrap
        }

        Grid {
        id: presetGrid
        x: 12
        y: 12
        width: parent.width - 24
        visible: root.visibleTemplates.length > 0
        columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
        columnSpacing: Theme.assetCardGap
        rowSpacing: Theme.assetCardGap

        Repeater {
            model: root.visibleTemplates
            delegate: Column {
                id: templateCard
                required property var modelData
                width: Theme.assetCardWidth
                spacing: 4

                readonly property var effectThumbs: templateCard.modelData.effectThumbnails || []
                readonly property int effectCount: templateCard.modelData.effectCount || effectThumbs.length
                readonly property int mosaicCells: Math.min(4, Math.max(1, effectThumbs.length))
                readonly property int mosaicColumns: mosaicCells <= 1 ? 1 : 2
                readonly property int mosaicRows: mosaicCells <= 2 ? 1 : 2

                Rectangle {
                    width: Theme.assetCardWidth
                    height: Theme.assetCardWidth
                    radius: Theme.radiusSm
                    color: cardHover.hovered ? Theme.panelSecondaryBg : Theme.panelAccent
                    border.width: 1
                    border.color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.35)
                    clip: true
                    // Matches the media cards in MediaAssetsTab; these snapped.
                    scale: cardHover.hovered ? 1.03 : 1.0

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                    Behavior on scale {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    HoverHandler { id: cardHover }

                    Grid {
                        id: thumbMosaic
                        anchors.fill: parent
                        columns: templateCard.mosaicColumns
                        rows: templateCard.mosaicRows

                        Repeater {
                            model: templateCard.mosaicCells
                            delegate: Item {
                                required property int index
                                width: thumbMosaic.width / templateCard.mosaicColumns
                                height: thumbMosaic.height / templateCard.mosaicRows

                                Image {
                                    anchors.fill: parent
                                    visible: index < templateCard.effectThumbs.length
                                    source: visible
                                            ? EditorState.imageUrl(templateCard.effectThumbs[index])
                                            : ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    smooth: true
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    visible: index === 3 && templateCard.effectCount > 4
                                    color: Theme.scrimStrong

                                    Text {
                                        anchors.centerIn: parent
                                        text: "+" + (templateCard.effectCount - 3)
                                        color: Theme.onMedia
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeCard
                                        font.weight: Font.DemiBold
                                    }
                                }

                                Rectangle {
                                    anchors.right: parent.right
                                    width: 1
                                    height: parent.height
                                    color: Theme.panelBackground
                                    visible: index % templateCard.mosaicColumns
                                              !== templateCard.mosaicColumns - 1
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: Theme.panelBackground
                                    visible: index < templateCard.mosaicCells - templateCard.mosaicColumns
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: templateCard.effectThumbs.length === 0
                        width: parent.width - 12
                        text: templateCard.modelData.label
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeCard
                        font.weight: Font.Medium
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 4
                        width: stackBadgeRow.implicitWidth + 8
                        height: stackBadgeRow.implicitHeight + 4
                        radius: Theme.radiusXs
                        color: Theme.scrimStrong

                        Row {
                            id: stackBadgeRow
                            anchors.centerIn: parent
                            spacing: 3

                            IconGlyph {
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: Theme.icons.layers
                                iconSize: 10
                                iconColor: Theme.onMedia
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: templateCard.effectCount > 0 ? templateCard.effectCount : "?"
                                color: Theme.onMedia
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                font.weight: Font.Medium
                            }
                        }
                    }

                    TapHandler {
                        onTapped: root.applyTemplate(templateCard.modelData.id)
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 4
                        visible: templateCard.modelData.requiresSegmentation === true
                        width: segBadge.implicitWidth + 8
                        height: segBadge.implicitHeight + 4
                        radius: Theme.radiusXs
                        color: Theme.scrimStrong

                        Text {
                            id: segBadge
                            anchors.centerIn: parent
                            text: qsTr("Needs cutout")
                            color: Theme.onMedia
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            font.weight: Font.Medium
                        }
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
                        onClicked: root.applyTemplate(templateCard.modelData.id)
                    }

                    AssetFavoriteButton {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 3
                        tabId: "templates"
                        itemId: templateCard.modelData.id
                    }
                }

                Text {
                    width: parent.width
                    text: templateCard.modelData.label
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
}

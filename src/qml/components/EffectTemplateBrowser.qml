import QtQuick
import QtQuick.Controls.Basic
import Drift

// Beat-synced multi-effect presets: category chips + card grid.
Column {
    id: root
    spacing: 0

    readonly property var categories: EditorState.effectTemplateCategories()
    property string activeCategory: categories.length > 0 ? categories[0].id : ""

    function applyTemplate(templateId) {
        if (EditorState.selectedClip < 0)
            return
        EditorState.applyEffectTemplate(EditorState.selectedTrack, EditorState.selectedClip, templateId)
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
              ? qsTr("Click a template to apply music-synced effects to the selection")
              : qsTr("Select a clip, then click a template to apply")
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
                model: EditorState.effectTemplateCatalog()
                delegate: Column {
                    id: templateCard
                    required property var modelData
                    visible: templateCard.modelData.category === root.activeCategory
                    width: visible ? Theme.assetCardWidth : 0
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

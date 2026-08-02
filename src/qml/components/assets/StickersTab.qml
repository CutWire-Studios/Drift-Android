import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

// Stickers tab: category page chips over a grid of the built-in sticker set.
Item {
    id: root

    // Stickers come from an addon, so these are refreshed on install rather than
    // being fixed at load: a fresh install has no packs and no pages at all.
    property var categories: EditorState.builtinStickerCategories()
    property var allStickers: EditorState.builtinStickers()
    readonly property var pages: categories
    readonly property bool hasStickers: allStickers.length > 0
    property int pageIndex: 0
    readonly property string currentPageId: pages[pageIndex] ? pages[pageIndex].id : ""

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "stickers")
                return
            root.categories = EditorState.builtinStickerCategories()
            root.allStickers = EditorState.builtinStickers()
            root.pageIndex = 0
        }
    }

    Flow {
        id: stickerPageBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        spacing: 6

        Repeater {
            model: root.pages
            delegate: ThemedChip {
                required property var modelData
                required property int index
                text: modelData.label
                variant: "secondary"
                selected: root.pageIndex === index
                onClicked: root.pageIndex = index
            }
        }
    }

    Flickable {
        anchors.top: stickerPageBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: 12
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        contentHeight: stickerPageContent.height + 24
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: stickerPageContent
            width: parent.width - 12
            spacing: 16

            EmptyState {
                visible: !root.hasStickers
                width: parent.width
                compact: true
                glyph: Theme.icons.smile
                title: qsTr("No sticker packs installed")
                hint: qsTr("Install the emoji pack to add stickers.")
                actionText: qsTr("Get extras")
                actionVariant: "primary"
                onActionTriggered: root.Window.window.openAddonManager("stickers")
            }

            // A category page with no matching stickers used to render
            // as a silently blank grid.
            readonly property var currentStickers:
                root.allStickers.filter(function(s) { return s.category === root.currentPageId })

            EmptyState {
                visible: root.hasStickers
                         && stickerPageContent.currentStickers.length === 0
                width: parent.width
                compact: true
                glyph: Theme.icons.smile
                title: qsTr("Nothing in this category")
                hint: qsTr("Pick another category above.")
            }

            // Sticker grid for the selected category page.
            Grid {
                width: parent.width
                columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
                columnSpacing: Theme.assetCardGap
                rowSpacing: Theme.assetCardGap

                Repeater {
                    model: stickerPageContent.currentStickers
                    delegate: Column {
                        required property var modelData
                        width: Theme.assetCardWidth
                        spacing: Theme.spacingSm

                        Rectangle {
                            width: Theme.assetCardWidth
                            height: Theme.assetCardWidth
                            radius: Theme.radiusSm
                            color: stickerMouse.containsMouse ? Theme.popoverHover : Theme.panelAccent
                            clip: true

                            Behavior on color {
                                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }

                            SkeletonBox {
                                anchors.fill: parent
                                anchors.margins: Theme.pagePadding
                                visible: stickerImage.status === Image.Loading
                            }

                            Image {
                                id: stickerImage
                                anchors.fill: parent
                                anchors.margins: Theme.pagePadding
                                source: EditorState.imageUrl(modelData.path)
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                opacity: status === Image.Ready ? 1 : 0

                                Behavior on opacity {
                                    NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                                }
                            }

                            IconGlyph {
                                anchors.centerIn: parent
                                visible: stickerImage.status === Image.Error
                                glyph: Theme.icons.error
                                iconSize: Theme.iconSizeLg
                                iconColor: Theme.mutedForeground
                            }

                            ThemedToolTip {
                                text: modelData.label
                                visible: stickerMouse.containsMouse
                            }

                            MouseArea {
                                id: stickerMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: EditorState.addStickerClip(modelData.id, -1)
                            }
                        }

                        Text {
                            width: parent.width
                            text: modelData.label
                            color: Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeCard
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}

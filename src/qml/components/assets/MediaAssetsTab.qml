import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Shared media browser: the kinds-filtered AssetLibrary grid/list used by the
// Media tab. The parent owns width/height/visible and the import orchestration,
// wiring add/import intents back through the callbacks below.
Item {
    id: root

    // Mirrors the header's view toggle, owned by the parent so the toolbar and
    // grid stay in sync.
    property bool gridMode: true
    // True while an import is running, so the empty state can step aside.
    property bool importing: false
    // Kind visibility filter supplied by the parent (depends on the active tab).
    property var assetVisibleFn: function(kind) { return true }

    // Emitted when a card/row is clicked or tapped to add its asset.
    signal addRequested(int assetIndex)
    // Emitted when the empty-state action asks to import media.
    signal importRequested()

    // First-run screen for a project with no media. This area used to
    // render as a blank rectangle, with no hint that the panel accepts
    // drops or that an Import button exists.
    EmptyState {
        width: parent.width
        height: parent.height
        visible: AssetLibrary.count === 0 && !root.importing
        glyph: Theme.icons.film
        title: qsTr("No media yet")
        hint: qsTr("Drag video, audio or images here, or use Import.")
        actionText: qsTr("Import media")
        actionVariant: "primary"
        onActionTriggered: root.importRequested()
    }

    Flickable {
        id: flick
        visible: AssetLibrary.count > 0
        width: parent.width
        height: parent.height
        contentHeight: root.gridMode ? grid.height + Theme.spacing3xl
                                     : listColumn.height + Theme.spacing3xl
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Grid {
            id: grid
            visible: root.gridMode
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: flick.width - Theme.pagePadding * 2
            columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
            columnSpacing: Theme.assetCardGap
            rowSpacing: Theme.assetCardGap

            Repeater {
                model: AssetLibrary
                delegate: Column {
                    width: Theme.assetCardWidth
                    spacing: 4
                    visible: root.assetVisibleFn(kind)

                    required property int index
                    required property string name
                    required property string kind
                    required property string duration
                    required property double durationSeconds
                    required property string path
                    required property string thumbnailPath
                    required property string filmstripPath

                    property int assetIndex: index

                    Drag.active: assetDrag.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.keys: ["text/plain"]
                    Drag.mimeData: { "text/plain": assetIndex.toString() }

                    // Grid cards had neither hover feedback nor a pointing
                    // cursor, while the list rows had both.
                    HoverHandler {
                        id: cardHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    ThemedToolTip {
                        text: name
                        visible: cardHover.hovered
                    }

                    TapHandler { onTapped: root.addRequested(assetIndex) }
                    DragHandler {
                        id: assetDrag
                        // Without target: null the handler moves the card itself,
                        // clobbering the Grid positioner's x/y.
                        target: null
                        acceptedButtons: Qt.LeftButton
                        onActiveChanged: {
                            if (active) {
                                EditorState.draggingAssetIndex = assetIndex
                            } else {
                                Qt.callLater(function() {
                                    if (!assetDrag.active)
                                        EditorState.draggingAssetIndex = -1
                                })
                            }
                        }
                    }

                    Rectangle {
                        width: Theme.assetCardWidth
                        height: Theme.assetCardWidth * 9 / 16
                        radius: Theme.radiusSm
                        color: Theme.panelAccent
                        clip: true
                        border.width: cardHover.hovered ? Theme.borderWidth : 0
                        border.color: Theme.primary
                        scale: cardHover.hovered ? 1.03 : 1.0

                        Behavior on scale {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }
                        Behavior on border.width {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        // Placeholder while the thumbnail decodes. The
                        // card used to sit empty, indistinguishable from
                        // a failed load.
                        SkeletonBox {
                            anchors.fill: parent
                            radius: parent.radius
                            visible: thumbnailPath.length > 0
                                     && gridThumb.status === Image.Loading
                        }

                        Image {
                            id: gridThumb
                            anchors.fill: parent
                            visible: thumbnailPath.length > 0 && status === Image.Ready
                            source: thumbnailPath.length > 0 ? EditorState.imageUrl(thumbnailPath) : ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            // Fades in rather than popping at full opacity.
                            opacity: status === Image.Ready ? 1 : 0

                            Behavior on opacity {
                                NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                            }
                        }

                        IconGlyph {
                            anchors.centerIn: parent
                            // Also covers Image.Error, so a missing
                            // thumbnail file falls back to the kind icon
                            // instead of staying blank forever.
                            visible: thumbnailPath.length === 0
                                     || gridThumb.status === Image.Error
                            glyph: kind === "audio" ? Theme.icons.music
                                 : kind === "image" ? Theme.icons.image
                                 : Theme.icons.film
                            iconSize: Theme.spacing3xl
                            iconColor: Theme.mutedForeground
                        }

                        Rectangle {
                            visible: duration.length > 0
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: Theme.spacingSm
                            color: Theme.scrimStrong
                            radius: Theme.radiusXs
                            width: durationLabel.implicitWidth + Theme.spacingLg
                            height: durationLabel.implicitHeight + Theme.spacingSm
                            Text {
                                id: durationLabel
                                anchors.centerIn: parent
                                text: duration
                                color: Theme.onMedia
                                font.pixelSize: Theme.fontSizeXs
                                font.family: Theme.fontFamily
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: name
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeCard
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Column {
            id: listColumn
            visible: !root.gridMode
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: flick.width - Theme.pagePadding * 2
            spacing: Theme.spacingMd

            Repeater {
                model: AssetLibrary
                delegate: Rectangle {
                    id: listRow
                    width: listColumn.width
                    height: 48
                    radius: Theme.radiusSm
                    color: rowMouse.containsMouse ? Theme.popoverHover : Theme.panelAccent
                    visible: root.assetVisibleFn(kind)

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    required property int index
                    required property string name
                    required property string kind
                    required property string duration
                    required property string thumbnailPath

                    property int assetIndex: index

                    Row {
                        id: listRowContent
                        anchors.fill: parent
                        anchors.margins: Theme.spacingLg
                        spacing: Theme.spacingLg + Theme.spacingXs

                        Rectangle {
                            id: listThumbFrame
                            width: 56
                            height: 32
                            radius: Theme.radiusSm
                            color: Theme.panelBackground
                            clip: true

                            SkeletonBox {
                                anchors.fill: parent
                                radius: parent.radius
                                visible: thumbnailPath.length > 0
                                         && listThumb.status === Image.Loading
                            }

                            Image {
                                id: listThumb
                                anchors.fill: parent
                                visible: thumbnailPath.length > 0 && status === Image.Ready
                                source: thumbnailPath.length > 0 ? EditorState.imageUrl(thumbnailPath) : ""
                                fillMode: Image.PreserveAspectFit
                                // Was missing, so list thumbnails decoded
                                // on the UI thread and stalled scrolling.
                                asynchronous: true
                            }

                            IconGlyph {
                                anchors.centerIn: parent
                                visible: thumbnailPath.length === 0
                                         || listThumb.status === Image.Error
                                glyph: kind === "audio" ? Theme.icons.music : Theme.icons.film
                                iconSize: Theme.iconSizeBase
                                iconColor: Theme.mutedForeground
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            // Derived from the actual thumbnail width
                            // rather than a magic constant.
                            width: parent.width - listThumbFrame.width - listRowContent.spacing
                            Text {
                                text: name
                                color: Theme.panelForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                                elide: Text.ElideRight
                                width: parent.width
                            }
                            Text {
                                text: kind + (duration.length > 0 ? " · " + duration : "")
                                color: Theme.mutedForeground
                                font.pixelSize: Theme.fontSizeXs
                                font.family: Theme.fontFamily
                                // Was unbounded, so it overflowed the row
                                // at narrow panel widths.
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }
                    }

                    ThemedToolTip {
                        text: name
                        visible: rowMouse.containsMouse
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.addRequested(assetIndex)
                    }
                }
            }
        }
    }
}

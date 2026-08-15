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
    readonly property string query: search.text.trim().toLowerCase()

    // Emitted when a card/row is clicked or tapped to add its asset.
    signal addRequested(int assetIndex)
    // Emitted from the card/row context menu. The parent owns the in-use
    // check and the confirmation.
    signal removeRequested(int assetIndex)
    // Emitted from the card/row context menu. The parent owns the file picker.
    signal replaceRequested(int assetIndex)
    // Emitted from the card/row context menu. The parent owns the rename dialog.
    signal renameRequested(int assetIndex)
    // Emitted from the card/row context menu, image rows only. The parent owns the save dialog.
    signal exportRequested(int assetIndex)
    // Emitted when the empty-state action asks to import media.
    signal importRequested()

    function assetMatches(name, kind) {
        if (!root.assetVisibleFn(kind))
            return false
        if (root.query.length === 0)
            return true
        return name.toLowerCase().indexOf(root.query) >= 0
    }

    // How many bin rows pass kind + search — drives the "no matches" empty state.
    readonly property int matchCount: {
        const q = root.query
        let n = 0
        for (let i = 0; i < AssetLibrary.count; ++i) {
            const asset = AssetLibrary.assetAt(i)
            if (!root.assetVisibleFn(asset.kind))
                continue
            if (q.length > 0 && asset.name.toLowerCase().indexOf(q) < 0)
                continue
            ++n
        }
        return n
    }

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

    ThemedTextField {
        id: search
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.pagePadding
        visible: AssetLibrary.count > 0
        placeholderText: qsTr("Search media")
        font.family: Theme.fontFamily
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: AssetLibrary.count > 0 && root.matchCount === 0
        compact: true
        glyph: Theme.icons.search
        title: qsTr("No media match “%1”").arg(search.text.trim())
        hint: qsTr("Try a different name.")
    }

    // Tapping a card opens its menu on touch; the timeline is reached by holding
    // it. Without this the gesture is invisible and the tab reads as broken.
    Text {
        id: touchHint
        anchors.top: search.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        anchors.topMargin: visible ? Theme.spacingSm : 0
        visible: Theme.touchUi && AssetLibrary.count > 0
        height: visible ? implicitHeight : 0
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("Touch and hold a clip, then drag it onto the timeline.")
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Flickable {
        id: flick
        visible: AssetLibrary.count > 0 && root.matchCount > 0
        anchors.top: touchHint.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.spacingMd
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
                    id: mediaCard
                    width: visible ? Theme.assetCardWidth : 0
                    spacing: 4
                    visible: root.assetMatches(name, kind)

                    required property int index
                    required property string name
                    required property string kind
                    required property string duration
                    required property double durationSeconds
                    required property string path
                    required property string thumbnailPath
                    required property string filmstripPath

                    property int assetIndex: index

                    // Lift on grab: dims and grows slightly, so the card reads as
                    // picked up rather than just sitting there while a ghost moves.
                    opacity: assetDrag.active ? 0.85 : 1
                    scale: assetDrag.active ? 1.04 : 1.0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                    Behavior on scale {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    Drag.active: assetDrag.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.keys: ["text/plain"]
                    Drag.mimeData: { "text/plain": assetIndex.toString() }
                    // Default hotspot is (0,0) at the card top-left; Wayland
                    // compositors (notably Mutter) then deliver drop Y far from
                    // the grab point. Center on the thumbnail like effect cards.
                    Drag.hotSpot.x: width / 2
                    Drag.hotSpot.y: Theme.assetCardWidth * 9 / 32

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

                        // Reading the replacement off disk is the one wait in the swap with
                        // nothing on screen to show for it. Scrims this card only, so the rest of
                        // the bin stays usable.
                        Rectangle {
                            id: gridBusy
                            // Above the duration badge, which is a later sibling.
                            z: 1
                            anchors.fill: parent
                            radius: parent.radius
                            color: Theme.scrimStrong
                            readonly property bool busy:
                                EditorState.replacingAssetId.length > 0
                                && EditorState.replacingAssetId === AssetLibrary.assetIdAt(assetIndex)
                            visible: opacity > 0
                            opacity: busy ? 1 : 0

                            Behavior on opacity {
                                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }

                            // Swallows taps and drags while the swap is in flight, so the card
                            // cannot be added to the timeline against media that is changing.
                            MouseArea {
                                anchors.fill: parent
                                enabled: gridBusy.busy
                                acceptedButtons: Qt.AllButtons
                            }

                            CircularProgress {
                                anchors.centerIn: parent
                                indeterminate: true
                                size: Theme.spacing3xl
                                progressColor: Theme.onMedia
                            }
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

                        // Both handlers live on this child, not on the Column
                        // that owns the Drag attached property. With them on the
                        // Column, QDrag::exec() ungrabs the very item Drag.active
                        // is attached to, the handler deactivates inside
                        // setActive(true), and the `Drag.active: assetDrag.active`
                        // binding re-enters it — "Binding loop detected for
                        // property active", and the drag dies mid-flight.
                        // EffectBrowser and ShapesTab already nest them this way.
                        TapHandler {
                            // Unguarded, the tap competes with the drag for the
                            // grab and fires an add on release after a drag.
                            enabled: !Theme.touchUi && !assetDrag.active
                            onTapped: root.addRequested(assetIndex)
                        }
                        DragHandler {
                            id: assetDrag
                            // Without target: null the handler moves the card itself,
                            // clobbering the Grid positioner's x/y.
                            target: null
                            // Touch lifts through TouchDrag below instead: a platform
                            // drag has no touch gesture and cannot leave the sheet.
                            enabled: !Theme.touchUi
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
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: cardMenu.popup()
                        }

                        // Touch: hold to lift the card out of the sheet and onto the
                        // timeline, tap for the menu that right-click gives desktop.
                        // Tapping used to add at the playhead, which put clips on a
                        // track and a time nobody chose.
                        TouchLiftArea {
                            enabled: Theme.touchUi && !gridBusy.busy
                            dragKind: "media"
                            payload: mediaCard.assetIndex
                            label: mediaCard.name
                            thumbnail: mediaCard.thumbnailPath
                            glyph: mediaCard.kind === "audio" ? Theme.icons.music
                                 : mediaCard.kind === "image" ? Theme.icons.image
                                 : Theme.icons.film
                            onLiftTapped: cardMenu.popup()
                        }

                        ThemedContextMenu {
                            id: cardMenu

                            ThemedMenuItem {
                                text: qsTr("Rename…")
                                icon.name: Theme.icons.pencil
                                onTriggered: root.renameRequested(assetIndex)
                            }
                            ThemedMenuItem {
                                text: qsTr("Replace media…")
                                icon.name: Theme.icons.refresh
                                onTriggered: root.replaceRequested(assetIndex)
                            }
                            ThemedMenuItem {
                                text: qsTr("Export image…")
                                icon.name: Theme.icons.save
                                visible: kind === "image"
                                onTriggered: root.exportRequested(assetIndex)
                            }
                            ThemedMenuItem {
                                text: qsTr("Remove from project")
                                icon.name: Theme.icons.trash
                                onTriggered: root.removeRequested(assetIndex)
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
                    height: visible ? 48 : 0
                    radius: Theme.radiusSm
                    color: rowMouse.containsMouse ? Theme.popoverHover : Theme.panelAccent
                    visible: root.assetMatches(name, kind)

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    required property int index
                    required property string name
                    required property string kind
                    required property string duration
                    required property string thumbnailPath

                    property int assetIndex: index
                    readonly property bool replaceBusy:
                        EditorState.replacingAssetId.length > 0
                        && EditorState.replacingAssetId === AssetLibrary.assetIdAt(assetIndex)

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

                            // Same busy treatment as the grid card, scaled to the row thumbnail.
                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                color: Theme.scrimStrong
                                visible: opacity > 0
                                opacity: replaceBusy ? 1 : 0

                                Behavior on opacity {
                                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }

                                CircularProgress {
                                    anchors.centerIn: parent
                                    indeterminate: true
                                    size: Theme.iconSizeBase
                                    strokeWidth: 2
                                    progressColor: Theme.onMedia
                                }
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
                        // Adding to the timeline mid-swap would bind a clip to media that is
                        // about to change under it; the right-click menu goes with it.
                        // Touch goes through TouchLiftArea below instead.
                        enabled: !replaceBusy && !Theme.touchUi
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton)
                                rowMenu.popup()
                            else
                                root.addRequested(assetIndex)
                        }
                    }

                    // Touch takes the row the same way as a grid card: hold to lift
                    // it onto the timeline, tap for the menu.
                    TouchLiftArea {
                        enabled: Theme.touchUi && !listRow.replaceBusy
                        dragKind: "media"
                        payload: listRow.assetIndex
                        label: listRow.name
                        thumbnail: listRow.thumbnailPath
                        glyph: listRow.kind === "audio" ? Theme.icons.music
                             : listRow.kind === "image" ? Theme.icons.image
                             : Theme.icons.film
                        onLiftTapped: rowMenu.popup()
                    }

                    ThemedContextMenu {
                        id: rowMenu

                        ThemedMenuItem {
                            text: qsTr("Rename…")
                            icon.name: Theme.icons.pencil
                            onTriggered: root.renameRequested(assetIndex)
                        }
                        ThemedMenuItem {
                            text: qsTr("Replace media…")
                            icon.name: Theme.icons.refresh
                            onTriggered: root.replaceRequested(assetIndex)
                        }
                        ThemedMenuItem {
                            text: qsTr("Export image…")
                            icon.name: Theme.icons.save
                            visible: kind === "image"
                            onTriggered: root.exportRequested(assetIndex)
                        }
                        ThemedMenuItem {
                            text: qsTr("Remove from project")
                            icon.name: Theme.icons.trash
                            onTriggered: root.removeRequested(assetIndex)
                        }
                    }
                }
            }
        }
    }
}

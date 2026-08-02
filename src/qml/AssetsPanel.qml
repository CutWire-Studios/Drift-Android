import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtQuick.Dialogs
import Drift
import "components"
import "components/assets"

PanelFrame {
    id: root

    // When true (Android bottom sheet), hide the side tab rail — the phone
    // bottom rail already picks the active library tab.
    property bool sheetMode: false
    border.width: sheetMode ? 0 : 1
    radius: sheetMode ? 0 : Theme.radiusSm
    color: sheetMode ? "transparent" : Theme.panelBackground

    Component.onCompleted: AssetLibrary.ensureAllMedia()

    function addAssetToTimeline(assetIndex) {
        if (typeof Window !== "undefined" && Window.window && Window.window.configureAndAddAsset) {
            Window.window.configureAndAddAsset(assetIndex, () => EditorState.addClipFromAsset(assetIndex))
        } else {
            EditorState.addClipFromAsset(assetIndex)
        }
    }

    // Imports and reports the outcome. `importUrls` skips anything it cannot
    // probe, so a bad file used to just never appear with no explanation at all.
    // Comparing the row count before and after tells us how many were rejected.
    function importUrlsReporting(urls) {
        if (!urls || urls.length === 0)
            return
        root.importing = true
        const before = AssetLibrary.count
        AssetLibrary.importUrls(urls)
        const added = AssetLibrary.count - before
        root.importing = false

        const skipped = urls.length - added
        if (added > 0 && skipped > 0)
            Toasts.warning(qsTr("Imported %1 of %2 files. %3 could not be read.")
                           .arg(added).arg(urls.length).arg(skipped))
        else if (added > 0)
            Toasts.success(qsTr("Imported %n file(s).", "", added))
        else if (urls.length === 1)
            Toasts.error(qsTr("Could not import that file — the format may be unsupported."))
        else
            Toasts.error(qsTr("Could not import any of the %n selected file(s).", "", urls.length))
    }

    // True while an import is running, so the panel can show progress.
    property bool importing: false

    function importMedia() {
        var urls = FileDialogs.openFiles(qsTr("Import Media"), [
            qsTr("Media files (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mp3 *.wav *.aac *.flac *.ogg *.m4a *.png *.jpg *.jpeg *.gif *.webp *.bmp)")
        ])
        root.importUrlsReporting(urls)
    }

    // Selects a tab by id. Used by cross-panel jumps such as the properties
    // panel's "Browse effects" / "Browse sounds" empty-state actions.
    function showTab(tabId) {
        for (var i = 0; i < tabsModel.count; ++i) {
            if (tabsModel.get(i).tabId === tabId) {
                root.activeTab = i
                return
            }
        }
    }

    function tabLabel(tabId) {
        for (var i = 0; i < tabsModel.count; ++i) {
            if (tabsModel.get(i).tabId === tabId)
                return tabsModel.get(i).label
        }
        return ""
    }

    function kindsForTab(tabId) {
        if (tabId === "media") return ["video", "image", "audio"]
        return []
    }

    function assetVisible(kind) {
        const tabId = tabsModel.get(activeTab).tabId
        if (tabId === "text" || tabId === "stickers" || tabId === "shapes" || tabId === "effects"
                || tabId === "templates" || tabId === "adjustment" || tabId === "settings" || tabId === "sounds"
                || tabId === "transitions" || tabId === "shortcuts")
            return false
        const kinds = kindsForTab(tabId)
        return kinds.length === 0 || kinds.indexOf(kind) >= 0
    }

    ListModel {
        id: tabsModel
        ListElement { tabId: "media"; icon: 0; label: "Media" }
        ListElement { tabId: "sounds"; icon: 1; label: "Sounds" }
        ListElement { tabId: "text"; icon: 2; label: "Text" }
        ListElement { tabId: "stickers"; icon: 3; label: "Stickers" }
        ListElement { tabId: "shapes"; icon: 4; label: "Shapes" }
        ListElement { tabId: "effects"; icon: 5; label: "Effects" }
        ListElement { tabId: "templates"; icon: 9; label: "Templates" }
        ListElement { tabId: "transitions"; icon: 6; label: "Transitions" }
        ListElement { tabId: "settings"; icon: 7; label: "Settings" }
        ListElement { tabId: "shortcuts"; icon: 8; label: "Shortcuts" }
    }
    property var tabIcons: [
        Theme.icons.film,
        Theme.icons.headphones,
        Theme.icons.type,
        Theme.icons.smile,
        Theme.icons.shapes,
        Theme.icons.wand,
        Theme.icons.blend,
        Theme.icons.settings,
        Theme.icons.keyboard,
        Theme.icons.layers
    ]
    property int activeTab: 0
    property bool sortByKind: false

    DropArea {
        id: assetDropArea
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (drop) => {
            if (drop.hasUrls)
                root.importUrlsReporting(drop.urls)
        }
    }

    // Drag feedback. Dropping files onto the panel used to give no visual
    // confirmation that it was even a valid target.
    Rectangle {
        anchors.fill: parent
        z: 50
        radius: Theme.radiusMd
        color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.08)
        border.width: Theme.borderWidthFocus
        border.color: Theme.primary
        visible: opacity > 0
        opacity: assetDropArea.containsDrag ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        EmptyState {
            anchors.centerIn: parent
            glyph: Theme.icons.upload
            title: qsTr("Drop to import")
            hint: qsTr("Video, audio and image files")
        }
    }

    // Import progress. Probing and thumbnailing a large selection blocks for a
    // while; the panel used to simply appear frozen.
    Rectangle {
        anchors.fill: parent
        z: 60
        color: Theme.panelBackground
        opacity: root.importing ? 0.92 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        EmptyState {
            anchors.centerIn: parent
            glyph: Theme.icons.spinner
            title: qsTr("Importing…")
            hint: qsTr("Reading media and generating thumbnails.")
        }
    }

    Row {
        anchors.fill: parent
        spacing: 0

        // Vertical tab rail. Up/Down move between tabs once it has focus.
        // Hidden in sheetMode — AndroidBottomRail drives showTab() instead.
        Column {
            id: tabRail
            width: root.sheetMode ? 0 : Theme.tabRailWidth
            height: parent.height
            visible: !root.sheetMode
            topPadding: Theme.spacingSm
            spacing: Theme.spacingXs
            clip: true

            Accessible.role: Accessible.PageTabList

            Keys.onUpPressed: function(event) {
                root.activeTab = (root.activeTab - 1 + tabsModel.count) % tabsModel.count
                event.accepted = true
            }
            Keys.onDownPressed: function(event) {
                root.activeTab = (root.activeTab + 1) % tabsModel.count
                event.accepted = true
            }

            Repeater {
                model: tabsModel
                delegate: IconButton {
                    required property int index
                    required property var model

                    anchors.horizontalCenter: parent.horizontalCenter
                    glyph: root.tabIcons[model.icon]
                    variant: "ghost"
                    tooltip: model.label
                    active: root.activeTab === index
                    onClicked: root.activeTab = index

                    Accessible.role: Accessible.PageTab
                    Accessible.name: model.label
                    Accessible.checked: root.activeTab === index
                }
            }
        }

        Rectangle {
            width: root.sheetMode ? 0 : Theme.borderWidth
            height: parent.height
            visible: !root.sheetMode
            color: Theme.panelBorder
        }

        Column {
            id: assetsContent
            width: parent.width - (root.sheetMode ? 0 : (Theme.tabRailWidth + Theme.borderWidth))
            height: parent.height
            property bool gridMode: true

            Rectangle {
                width: parent.width
                height: Theme.panelHeaderHeight
                // Matches the surrounding PanelFrame; it used to paint the app
                // background, so the header read as a different surface than the
                // panel it belongs to.
                color: Theme.panelBackground

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.borderWidth
                    color: Theme.panelBorder
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.pagePadding
                    anchors.verticalCenter: parent.verticalCenter
                    // Sheet chrome already shows the tab title.
                    visible: !root.sheetMode
                    text: tabsModel.get(root.activeTab).label
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }

                // The sticker packs are a curated subset of the emoji set, so the rest live behind
                // this button rather than being unreachable.
                IconButton {
                    id: emojiPickerButton
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    visible: tabsModel.get(root.activeTab).tabId === "stickers"
                    glyph: Theme.icons.plus
                    variant: "ghost"
                    tooltip: qsTr("More emoji")
                    active: emojiPicker.opened
                    onClicked: emojiPicker.opened ? emojiPicker.close() : emojiPicker.open()
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    visible: kindsForTab(tabsModel.get(root.activeTab).tabId).length > 0

                    IconButton {
                        glyph: Theme.icons.grid
                        variant: "ghost"
                        tooltip: qsTr("Grid view")
                        active: assetsContent.gridMode
                        onClicked: assetsContent.gridMode = true
                    }
                    IconButton {
                        glyph: Theme.icons.list
                        variant: "ghost"
                        tooltip: qsTr("List view")
                        active: !assetsContent.gridMode
                        onClicked: assetsContent.gridMode = false
                    }
                    IconButton {
                        glyph: root.sortByKind ? Theme.icons.sortByKind : Theme.icons.sortByName
                        variant: "ghost"
                        tooltip: root.sortByKind ? qsTr("Sort by name") : qsTr("Sort by type")
                        onClicked: {
                            if (root.sortByKind)
                                AssetLibrary.sortByName()
                            else
                                AssetLibrary.sortByKind()
                            root.sortByKind = !root.sortByKind
                        }
                    }

                    ThemedButton {
                        text: qsTr("Import")
                        variant: "ghost"
                        glyph: Theme.icons.upload
                        tooltip: qsTr("Import video, audio or image files")
                        enabled: !root.importing
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: root.importMedia()
                    }
                }
            }

            EmojiPicker {
                id: emojiPicker
                // Hangs off the button at the panel's right edge, and slides back rather than
                // running off-window when the panel is dragged narrow.
                x: Math.max(-Theme.tabRailWidth, assetsContent.width - width - 8)
                y: Theme.panelHeaderHeight + 4
                onAddonManagerRequested: root.Window.window.openAddonManager("stickers")
            }

            TextAssetsTab {
                visible: tabsModel.get(activeTab).tabId === "text"
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
            }

            SoundsTab {
                visible: tabsModel.get(activeTab).tabId === "sounds"
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
            }

            StickersTab {
                visible: tabsModel.get(activeTab).tabId === "stickers"
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
            }

            ShapesTab {
                visible: tabsModel.get(activeTab).tabId === "shapes"
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
            }

            SettingsTab {
                visible: tabsModel.get(activeTab).tabId === "settings"
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
            }

            ShortcutsTab {
                visible: tabsModel.get(activeTab).tabId === "shortcuts"
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
            }

            // Effects browser
            EffectBrowser {
                visible: tabsModel.get(activeTab).tabId === "effects"
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
            }

            EffectTemplateBrowser {
                visible: tabsModel.get(activeTab).tabId === "templates"
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
            }

            // Transitions browser
            Item {
                id: transitionsBrowser
                visible: tabsModel.get(activeTab).tabId === "transitions"
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight

                readonly property var categories: EditorState.transitionCategories()
                property string activeCategory: categories.length > 0 ? categories[0].id : ""

                Column {
                    anchors.fill: parent
                    spacing: 0

                    Text {
                        id: transitionTip
                        width: parent.width
                        leftPadding: Theme.pagePadding
                        rightPadding: Theme.pagePadding
                        topPadding: Theme.spacingLg
                        bottomPadding: Theme.spacingSm
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        // Capped so a wrapping tip cannot grow until it swallows
                        // the grid below it at narrow panel widths.
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        text: qsTr("Drag onto where two clips overlap. They fade into each other by default.")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    Flickable {
                        id: transitionCategoryFlick
                        width: parent.width
                        height: 34
                        contentWidth: transitionCategoryRow.width + Theme.spacing3xl
                        clip: true

                        Row {
                            id: transitionCategoryRow
                            x: Theme.pagePadding
                            height: parent.height
                            spacing: Theme.spacingMd

                            Repeater {
                                model: transitionsBrowser.categories
                                delegate: ThemedChip {
                                    required property var modelData
                                    text: modelData.label
                                    variant: "secondary"
                                    selected: modelData.id === transitionsBrowser.activeCategory
                                    onClicked: transitionsBrowser.activeCategory = modelData.id
                                }
                            }
                        }
                    }

                    Item {
                        width: parent.width
                        height: Math.max(0, parent.height - transitionTip.height - transitionCategoryFlick.height)

                        // A category whose filter matches nothing used to leave a
                        // blank scroll area with no explanation.
                        EmptyState {
                            anchors.centerIn: parent
                            width: Math.min(parent.width - Theme.spacing3xl, 260)
                            visible: transitionsBrowser.categories.length === 0
                            glyph: Theme.icons.blend
                            title: qsTr("No transitions available")
                            hint: qsTr("Install a transitions pack to add more.")
                            actionText: qsTr("Get extras")
                            onActionTriggered: root.Window.window.openAddonManager()
                        }

                    Flickable {
                        anchors.fill: parent
                        visible: transitionsBrowser.categories.length > 0
                        contentHeight: transitionGrid.height + Theme.spacing3xl
                        clip: true
                        ScrollBar.vertical: AppScrollBar { }

                        Grid {
                            id: transitionGrid
                            x: Theme.pagePadding
                            y: Theme.pagePadding
                            width: parent.width - Theme.pagePadding * 2
                            columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
                            columnSpacing: Theme.assetCardGap
                            rowSpacing: Theme.assetCardGap

                            Repeater {
                                model: EditorState.transitionKinds()
                                delegate: Column {
                                    id: transitionCard
                                    required property var modelData
                                    visible: transitionCard.modelData.category === transitionsBrowser.activeCategory
                                    width: visible ? Theme.assetCardWidth : 0
                                    spacing: 4
                                    opacity: transitionDrag.active ? 0.85 : 1

                                    readonly property string strip: transitionCard.modelData.previewStripPath || ""
                                    readonly property int frameCount: Math.max(1, transitionCard.modelData.previewFrames || 1)

                                    // Cards rest on a frame partway through the transition; hovering
                                    // scrubs the whole strip, which is the only way to tell many of
                                    // these apart (a crossfade and a dip look the same at p = 0.5).
                                    property real scrub: 0.45
                                    readonly property int frameIndex:
                                        Math.max(0, Math.min(frameCount - 1, Math.round(scrub * (frameCount - 1))))

                                    NumberAnimation on scrub {
                                        running: transitionHover.hovered && transitionCard.frameCount > 1
                                        from: 0
                                        to: 1
                                        duration: 1400
                                        loops: Animation.Infinite
                                    }

                                    Connections {
                                        target: transitionHover
                                        function onHoveredChanged() {
                                            if (!transitionHover.hovered)
                                                transitionCard.scrub = 0.45
                                        }
                                    }

                                    Drag.active: transitionDrag.active
                                    Drag.dragType: Drag.Automatic
                                    Drag.supportedActions: Qt.CopyAction
                                    Drag.keys: ["application/x-drift-transition"]
                                    Drag.mimeData: ({ "application/x-drift-transition": transitionCard.modelData.kind })
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: Theme.assetCardWidth / 2

                                    Rectangle {
                                        width: Theme.assetCardWidth
                                        height: Theme.assetCardWidth
                                        radius: Theme.radiusSm
                                        color: transitionHover.hovered ? Theme.panelSecondaryBg : Theme.panelAccent
                                        border.width: transitionDrag.active ? Theme.borderWidth : 0
                                        border.color: Theme.transitionOverlap
                                        clip: true

                                        // The card already had a considered hover
                                        // scrub animation but no transition on its
                                        // own colours.
                                        Behavior on color {
                                            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                        }
                                        Behavior on border.width {
                                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                        }

                                        HoverHandler {
                                            id: transitionHover
                                            cursorShape: Qt.PointingHandCursor
                                        }

                                        ThemedToolTip {
                                            text: qsTr("%1 — drag onto an overlap between two clips").arg(transitionCard.modelData.label)
                                            visible: transitionHover.hovered
                                        }

                                        DragHandler {
                                            id: transitionDrag
                                            target: null
                                            acceptedButtons: Qt.LeftButton
                                        }

                                        SkeletonBox {
                                            anchors.fill: parent
                                            visible: transitionCard.strip.length > 0
                                                     && transitionStrip.status === Image.Loading
                                        }

                                        // The strip is one row of square cells; slide it rather than
                                        // re-decoding a sourceClipRect per frame.
                                        Image {
                                            id: transitionStrip
                                            visible: transitionCard.strip.length > 0
                                                     && status === Image.Ready
                                            source: transitionCard.strip.length > 0
                                                    ? EditorState.imageUrl(transitionCard.strip) : ""
                                            height: parent.height
                                            width: parent.height * transitionCard.frameCount
                                            x: -transitionCard.frameIndex * parent.height
                                            fillMode: Image.Stretch
                                            asynchronous: true
                                            smooth: true
                                        }

                                        IconGlyph {
                                            anchors.centerIn: parent
                                            visible: transitionCard.strip.length === 0
                                                     || transitionStrip.status === Image.Error
                                            glyph: Theme.icons.blend
                                            iconSize: Theme.iconSizeXl
                                            iconColor: Theme.transitionOverlap
                                        }
                                    }

                                    Text {
                                        width: parent.width
                                        text: transitionCard.modelData.label
                                        color: Theme.panelForeground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeCard
                                        font.weight: Font.Medium
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                    }
                }
            }

            // Shared media browser used by the Media tab.
            MediaAssetsTab {
                visible: kindsForTab(tabsModel.get(activeTab).tabId).length > 0
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                gridMode: assetsContent.gridMode
                importing: root.importing
                assetVisibleFn: function(kind) { return root.assetVisible(kind) }
                onAddRequested: (assetIndex) => root.addAssetToTimeline(assetIndex)
                onImportRequested: root.importMedia()
            }
        }
    }
}

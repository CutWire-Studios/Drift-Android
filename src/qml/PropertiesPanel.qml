import QtQuick
import QtQuick.Controls.Basic
import Drift
import "components"
import "components/properties"

PanelFrame {
    id: root

    // Android bottom sheet: keep the inspector tab rail but drop the panel border
    // chrome that fights the sheet frame.
    property bool sheetMode: false
    border.width: sheetMode ? 0 : 1
    radius: sheetMode ? 0 : Theme.radiusSm
    color: sheetMode ? "transparent" : Theme.panelBackground

    // Raised by the Effects / Audio empty states; Main wires them to the
    // assets panel so the browse CTAs actually take the user somewhere.
    signal browseEffectsRequested()
    signal browseAudioEffectsRequested()

    // selectedClipData is a QVariantMap; key the binding on an explicit revision
    // so nested fields such as effects refresh after project edits.
    property int clipDataRevision: 0
    property int previousTab: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!root.clipData && Object.keys(root.clipData).length > 0
    readonly property var transition: EditorState.selectedTransitionData
    readonly property bool hasTransitionSelection: !!transition && Object.keys(transition).length > 0
    readonly property int transitionTabIndex: tabIndexOf("transition")
    readonly property string clipKind: hasSelection ? (root.clipData.kind || "") : ""
    readonly property bool hasTextStyle: hasSelection
                                         && (clipKind === "text" || clipKind === "subtitle")
                                         && !!root.clipData.textStyle

    property int activeTab: 0
    readonly property string currentTabId: tabsModel.get(activeTab).tabId

    onActiveTabChanged: {
        if (tabsModel.get(root.previousTab).tabId === "transition")
            transitionInspector.commitEdits()
        root.previousTab = activeTab
        transitionInspector.refreshFields()
    }

    // Kept for SubtitleEditor, which formats cue times through it.
    function formatSeconds(value) {
        return Number(value || 0).toFixed(2)
    }

    Connections {
        target: EditorState
        function onSelectionChanged() {
            root.clipDataRevision++
            root.syncSubtitlesTab()
            root.syncShapeTab()
            root.syncTextTab()
            root.syncAnimationTab()
        }
        function onSelectedClipDataChanged() {
            root.clipDataRevision++
            root.syncSubtitlesTab()
        }
        function onSelectedTransitionDataChanged() {
            if (root.hasTransitionSelection)
                root.activeTab = root.transitionTabIndex
        }
        function onTracksChanged() {
            root.clipDataRevision++
        }
    }

    Component.onCompleted: {
        root.syncSubtitlesTab()
    }

    // Rail order: clip identity → geometry/timing → compositing / FX.
    // Contextual tabs (text / shape / captions) share the first group so a
    // separator still appears after General when they are hidden.
    // `group` drives hairlines between the next *visible* tab of a different group.
    ListModel {
        id: tabsModel
        ListElement { tabId: "general"; icon: 0; label: "General"; group: 0 }
        ListElement { tabId: "text"; icon: 1; label: "Text"; group: 0 }
        ListElement { tabId: "shape"; icon: 2; label: "Shape"; group: 0 }
        ListElement { tabId: "subtitles"; icon: 3; label: "Subtitles"; group: 0 }
        ListElement { tabId: "transform"; icon: 4; label: "Transform"; group: 1 }
        ListElement { tabId: "animation"; icon: 5; label: "Animation"; group: 1 }
        ListElement { tabId: "audio"; icon: 6; label: "Audio"; group: 1 }
        ListElement { tabId: "speed"; icon: 7; label: "Speed"; group: 1 }
        ListElement { tabId: "blending"; icon: 8; label: "Blend"; group: 2 }
        ListElement { tabId: "masks"; icon: 9; label: "Cutouts"; group: 2 }
        ListElement { tabId: "effects"; icon: 10; label: "Effects"; group: 2 }
        ListElement { tabId: "audioEffects"; icon: 11; label: "Audio FX"; group: 2 }
        ListElement { tabId: "transition"; icon: 12; label: "Transition"; group: 2 }
    }
    property var tabIcons: [
        Theme.icons.info,
        Theme.icons.type,
        Theme.icons.shapes,
        Theme.icons.captions,
        Theme.icons.maximize,
        Theme.icons.sparkles,
        Theme.icons.volumeHigh,
        Theme.icons.gauge,
        Theme.icons.blend,
        Theme.icons.mask,
        Theme.icons.wand,
        Theme.icons.audioLines,
        Theme.icons.chevronsRight
    ]

    function tabVisible(tabId) {
        if (tabId === "subtitles")
            return root.clipKind === "subtitle"
        if (tabId === "shape")
            return root.clipKind === "shape"
        if (tabId === "text")
            return root.hasTextStyle
        if (tabId === "animation")
            return root.clipKind === "video" || root.clipKind === "image"
                   || root.clipKind === "shape" || root.clipKind === "text"
                   || root.clipKind === "audio"
        return true
    }

    // Hairline after this index when the next visible tab belongs to another group.
    function showSeparatorAfter(index) {
        const cur = tabsModel.get(index)
        if (!tabVisible(cur.tabId))
            return false
        for (let i = index + 1; i < tabsModel.count; ++i) {
            const next = tabsModel.get(i)
            if (!tabVisible(next.tabId))
                continue
            return next.group !== cur.group
        }
        return false
    }

    function tabIndexOf(id) {
        for (let i = 0; i < tabsModel.count; i++) {
            if (tabsModel.get(i).tabId === id)
                return i
        }
        return -1
    }
    readonly property int subtitlesTabIndex: tabIndexOf("subtitles")
    readonly property int shapeTabIndex: tabIndexOf("shape")
    readonly property int textTabIndex: tabIndexOf("text")
    readonly property int animationTabIndex: tabIndexOf("animation")

    // The Subtitles tab only exists for subtitle clips, so leaving it selected would show a blank
    // pane once the selection moves off one. Selecting a subtitle clip never opens the tab by
    // itself: it took over whatever pane you were working in, and with it the timeline lane.
    function syncSubtitlesTab() {
        if (root.subtitlesTabIndex >= 0 && root.activeTab === root.subtitlesTabIndex
                && root.clipKind !== "subtitle")
            root.activeTab = 0
    }

    // The Shape tab is hidden for every other clip kind, so leaving it selected would show a blank
    // pane after the selection moves off a shape.
    function syncShapeTab() {
        if (root.shapeTabIndex >= 0 && root.activeTab === root.shapeTabIndex
                && root.clipKind !== "shape")
            root.activeTab = 0
    }

    // Same for the Text tab, which only exists for clips carrying a text style.
    function syncTextTab() {
        if (root.textTabIndex >= 0 && root.activeTab === root.textTabIndex && !root.hasTextStyle)
            root.activeTab = 0
    }

    function syncAnimationTab() {
        if (root.animationTabIndex >= 0 && root.activeTab === root.animationTabIndex
                && !root.tabVisible("animation"))
            root.activeTab = 0
    }

    // Tell the timeline to show its subtitle-cue lane only while the Subtitles tab is open.
    Binding {
        target: EditorState
        property: "subtitleEditing"
        value: root.currentTabId === "subtitles" && root.clipKind === "subtitle"
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(260, root.width - 32)
        visible: !root.hasSelection
        spacing: 16

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48
            height: 48
            radius: Theme.radiusMd
            color: "transparent"
            border.width: 1
            border.color: Theme.panelBorder

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.sliders
                iconSize: 22
                iconColor: Theme.mutedForeground
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("It's empty here")
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            font.weight: Font.Medium
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.sheetMode
                  ? qsTr("Tap a clip on the timeline to edit its properties")
                  : qsTr("Click a clip on the timeline to edit its properties")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Item {
        id: content
        anchors.fill: parent
        visible: root.hasSelection

        // === tab rail + tab content, similar UX to AssetsPanel =========================
        Row {
            id: tabsRow
            anchors.fill: parent
            spacing: 0

            // Up/Down move between tabs once the rail has focus.
            // Flickable so short panel heights can still reach lower icons.
            Flickable {
                id: propertiesTabRail
                width: Theme.tabRailWidth
                height: parent.height
                contentWidth: width
                contentHeight: propertiesTabRailColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height
                ScrollBar.vertical: AppScrollBar {
                    policy: propertiesTabRail.contentHeight > propertiesTabRail.height
                            ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                }

                Accessible.role: Accessible.PageTabList

                Keys.onUpPressed: function(event) {
                    let next = root.activeTab
                    for (let step = 0; step < tabsModel.count; ++step) {
                        next = (next - 1 + tabsModel.count) % tabsModel.count
                        if (root.tabVisible(tabsModel.get(next).tabId)) {
                            root.activeTab = next
                            break
                        }
                    }
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) {
                    let next = root.activeTab
                    for (let step = 0; step < tabsModel.count; ++step) {
                        next = (next + 1) % tabsModel.count
                        if (root.tabVisible(tabsModel.get(next).tabId)) {
                            root.activeTab = next
                            break
                        }
                    }
                    event.accepted = true
                }

                Column {
                    id: propertiesTabRailColumn
                    width: parent.width
                    topPadding: Theme.spacingSm
                    spacing: Theme.spacingXs

                    Repeater {
                        model: tabsModel
                        delegate: Column {
                            required property int index
                            required property var model

                            width: parent.width
                            spacing: 0
                            // Collapse so hidden contextual tabs leave no rail gap.
                            visible: root.tabVisible(model.tabId)
                            height: visible ? implicitHeight : 0

                            IconButton {
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

                            Item {
                                visible: root.showSeparatorAfter(index)
                                width: parent.width
                                height: visible ? Theme.spacingLg + Theme.borderWidth : 0

                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Theme.iconSizeSm
                                    height: Theme.borderWidth
                                    radius: height / 2
                                    color: Theme.panelBorder
                                    opacity: 0.85
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: Theme.borderWidth
                height: parent.height
                color: Theme.panelBorder
            }

            Flickable {
                id: tabFlick
                width: parent.width - Theme.tabRailWidth - Theme.borderWidth
                height: parent.height
                visible: root.currentTabId !== "subtitles"
                contentWidth: width
                // Include topPadding so the last controls stay reachable (SettingsTab pattern).
                contentHeight: tabColumn.height + Theme.spacing3xl
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height
                ScrollBar.vertical: AppScrollBar {
                    policy: tabFlick.contentHeight > tabFlick.height
                            ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                }

                // Tall tabs (Audio/Effects) leave contentY deep; reset when switching.
                Connections {
                    target: root
                    function onActiveTabChanged() {
                        tabFlick.contentY = 0
                        tabColumn.opacity = 0
                        tabFadeIn.restart()
                    }
                }

                // Fade the new tab in rather than hard-cutting to it. Fade-in only,
                // not a crossfade: the inspectors share a Column, which excludes
                // invisible items from layout, so overlapping two of them would
                // double-count height and jump the panel mid-transition.
                NumberAnimation {
                    id: tabFadeIn
                    target: tabColumn
                    property: "opacity"
                    from: 0.0
                    to: 1.0
                    duration: Theme.durationBase
                    easing.type: Theme.easing
                }

                Column {
                    id: tabColumn
                    x: Theme.pagePadding
                    width: parent.width - Theme.pagePadding * 2
                    spacing: Theme.spacingXl
                    topPadding: Theme.pagePadding

                    Text {
                        text: tabsModel.get(root.activeTab).label
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Medium
                    }

                    GeneralInspector {
                        width: tabColumn.width
                        visible: root.currentTabId === "general"
                    }

                    TextInspector {
                        width: tabColumn.width
                        visible: root.currentTabId === "text"
                    }

                    TransformInspector {
                        width: tabColumn.width
                        visible: root.currentTabId === "transform"
                    }

                    AnimationInspector {
                        width: tabColumn.width
                        visible: root.currentTabId === "animation"
                    }

                    AudioInspector {
                        width: tabColumn.width
                        visible: root.currentTabId === "audio"
                    }

                    SpeedFadeInspector {
                        width: tabColumn.width
                        visible: root.currentTabId === "speed"
                    }

                    TransitionInspector {
                        id: transitionInspector
                        width: tabColumn.width
                        visible: root.currentTabId === "transition"
                    }

                    BlendingInspector {
                        width: tabColumn.width
                        visible: root.currentTabId === "blending"
                    }

                    ShapeInspector {
                        width: tabColumn.width
                        visible: root.currentTabId === "shape"
                    }

                    MasksInspector {
                        width: tabColumn.width
                        visible: root.currentTabId === "masks"
                    }

                    EffectsInspector {
                        width: tabColumn.width
                        visible: root.currentTabId === "effects"
                        onBrowseEffectsRequested: root.browseEffectsRequested()
                    }

                    AudioEffectsInspector {
                        width: tabColumn.width
                        visible: root.currentTabId === "audioEffects"
                        onBrowseAudioEffectsRequested: root.browseAudioEffectsRequested()
                    }
                }
            }

            // Full-height editor with its own internal cue list scrolling, so it
            // sits beside the tab Flickable rather than inside it.
            SubtitleEditor {
                width: parent.width - Theme.tabRailWidth - Theme.borderWidth
                height: Math.max(0, parent.height)
                visible: root.currentTabId === "subtitles"
                clip: root.hasSelection ? root.clipData : null
                formatSeconds: root.formatSeconds
            }
        }
    }
}

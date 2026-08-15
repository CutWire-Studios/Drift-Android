import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Window
import Drift
import "components"

// CapCut-style phone editor: top bar, resizable preview | tools+timeline, bottom rail.
Item {
    id: root

    signal backRequested()
    signal goHomeRequested()

    property string sheetKind: "" // "" | "assets" | "properties"

    // Fullscreen preview: the timeline pane, top bar and rail step aside and
    // AndroidPreview takes the page. Readable from outside so AndroidMain's Back
    // dispatcher can be pointed at exitPreviewFullscreen().
    property bool previewFullscreen: false

    function exitPreviewFullscreen() {
        previewFullscreen = false
    }

    readonly property var projectFilter: [qsTr("Drift project (*.drift)")]

    function openAssetsTab(tabId) {
        sheetKind = "assets"
        rail.activeId = tabId
        assetsPanel.showTab(tabId)
        assetsSheet.title = assetsPanel.tabLabel(tabId)
        if (!assetsSheet.opened)
            assetsSheet.open()
        if (propertiesSheet.opened)
            propertiesSheet.dismiss()
    }

    // The rail's [+]. Clears whatever sheet is up first: the add menu is a step on
    // the way to one of them, and stacking it over an open Media sheet would leave
    // two sheets to dismiss to get back to the timeline.
    function openAddMenu() {
        sheetKind = ""
        rail.activeId = ""
        if (assetsSheet.opened)
            assetsSheet.dismiss()
        if (propertiesSheet.opened)
            propertiesSheet.dismiss()
        if (!addMenu.opened)
            addMenu.open()
    }

    function openPropertiesSheet() {
        sheetKind = "properties"
        rail.activeId = "edit"
        if (!propertiesSheet.opened)
            propertiesSheet.open()
        if (assetsSheet.opened)
            assetsSheet.dismiss()
    }

    function closeSheets() {
        sheetKind = ""
        rail.activeId = ""
        if (addMenu.opened)
            addMenu.dismiss()
        if (assetsSheet.opened)
            assetsSheet.dismiss()
        if (propertiesSheet.opened)
            propertiesSheet.dismiss()
    }

    // Android Back, delegated from AndroidMain. Returns true when it consumed the press,
    // so a sheet or dialog closes instead of the editor being popped out from under it.
    function handleBack() {
        if (topBar.menuOpen) {
            topBar.closeMenu()
            return true
        }
        if (exportProgressDialog.visible) {
            exportProgressDialog.close()
            return true
        }
        if (exportDialog.visible) {
            exportDialog.close()
            return true
        }
        if (packageProgressDialog.visible)
            return true // a package render is running; swallow rather than abandon it
        if (addMenu.opened) {
            addMenu.dismiss()
            return true
        }
        if (assetsSheet.opened || propertiesSheet.opened) {
            root.closeSheets()
            return true
        }
        if (EditorState.canvasCropMode) {
            EditorState.canvasCropMode = false
            return true
        }
        if (root.previewFullscreen) {
            root.exitPreviewFullscreen()
            return true
        }
        return false
    }

    function saveProject() {
        if (EditorState.currentProjectPath && EditorState.currentProjectPath.length > 0) {
            EditorState.saveProject(EditorState.fileUrl(EditorState.currentProjectPath))
            return !EditorState.hasUnsavedChanges
        }
        const url = FileDialogs.saveFile(qsTr("Save Project"), root.projectFilter,
                                         EditorState.projectName, "drift")
        if (url === "")
            return false
        EditorState.saveProject(url)
        return !EditorState.hasUnsavedChanges
    }

    function packageProject() {
        const url = FileDialogs.saveFile(qsTr("Save Shareable Copy"), root.projectFilter,
                                         EditorState.projectName, "drift")
        if (url !== "")
            EditorState.packageProject(url)
    }

    function openProject() {
        Window.window.confirmIfDirty(function () {
            const url = FileDialogs.openFile(qsTr("Open Project"), root.projectFilter)
            if (url !== "")
                EditorState.loadProject(url)
        })
    }

    function requestNewProject() {
        Window.window.confirmIfDirty(function () {
            EditorState.newProject()
            root.goHomeRequested()
        })
    }

    Column {
        anchors.fill: parent
        spacing: 0

        AndroidTopBar {
            id: topBar
            width: parent.width
            visible: !root.previewFullscreen
            onBackRequested: root.backRequested()
            onExportRequested: exportDialog.openDialog()
            onExportProgressRequested: {
                exportProgressDialog.dismissed = false
                exportProgressDialog.openDialog()
            }
            onSaveRequested: root.saveProject()
            onPackageRequested: root.packageProject()
            onOpenRequested: root.openProject()
            onNewRequested: root.requestNewProject()
            onLayoutRequested: Window.window.openLayoutChooser()
            onAssetsTabRequested: (tabId) => root.openAssetsTab(tabId)
        }

        SplitView {
            id: editorSplit
            width: parent.width
            // Column skips hidden children but their `height` still reads non-zero,
            // so the two strips only count while they are on screen.
            height: Math.max(0, parent.height - (topBar.visible ? topBar.height : 0)
                                - (rail.visible ? rail.height : 0))
            // Landscape and multi-window leave roughly 260px of height, which a vertical
            // split cannot divide into a usable preview and a usable timeline. Side by
            // side it divides the axis there is room on instead.
            readonly property bool sideBySide: width > height * 1.2
            orientation: sideBySide ? Qt.Horizontal : Qt.Vertical

            // Both minimums used to clamp to the whole pane (Math.min(height, ...)), so
            // together they could demand more than the pane had and SplitView let the last
            // item overflow behind the rail. They share a budget now: scaled down in
            // proportion whenever the two wants exceed what is actually available.
            readonly property real budget: Math.max(
                0, (sideBySide ? width : height) - Theme.androidSplitterHeight)
            readonly property real wantPreviewMin: Theme.androidPreviewTransportHeight + 72
            readonly property real wantTimelineMin: Theme.androidEditActionsHeight + 120
            // Side by side the axis being divided is width, and the two panes need very
            // different amounts of it: the preview's transport is a five-button row, the
            // timeline needs room for its track labels plus some visible seconds. Reusing
            // the vertical numbers let the preview shrink to 120px — narrower than its own
            // transport.
            readonly property real wantPreviewMinW: 220
            readonly property real wantTimelineMinW: Theme.androidTrackLabelsWidth + 160
            readonly property real minScale: {
                const want = sideBySide ? wantPreviewMinW + wantTimelineMinW
                                        : wantPreviewMin + wantTimelineMin
                return want > 0 ? Math.min(1, budget / want) : 1
            }
            readonly property real previewMin: wantPreviewMin * minScale
            readonly property real timelineMin: wantTimelineMin * minScale
            readonly property real previewMinW: wantPreviewMinW * minScale
            readonly property real timelineMinW: wantTimelineMinW * minScale

            handle: Item {
                implicitWidth: editorSplit.sideBySide ? Theme.androidSplitterHeight
                                                      : editorSplit.width
                implicitHeight: editorSplit.sideBySide ? editorSplit.height
                                                       : Theme.androidSplitterHeight

                Rectangle {
                    anchors.fill: parent
                    color: Theme.panelBackground
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: editorSplit.sideBySide ? 4 : 40
                    height: editorSplit.sideBySide ? 40 : 4
                    radius: 2
                    color: T.SplitHandle.pressed ? Theme.primary : Theme.panelBorder
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    cursorShape: editorSplit.sideBySide ? Qt.SplitHCursor : Qt.SplitVCursor
                }
            }

            AndroidPreview {
                id: preview
                fullscreen: root.previewFullscreen
                onFullscreenToggleRequested: root.previewFullscreen = !root.previewFullscreen

                // With the timeline pane hidden the preview is the only item left, and
                // its ceiling would otherwise still hold it to 72% of the split.
                // Avoid fixed mins larger than the first layout pass (height may be 0).
                SplitView.preferredHeight: root.previewFullscreen
                    ? editorSplit.height
                    : Math.min(implicitHeight,
                               Math.max(editorSplit.previewMin, editorSplit.height * 0.42))
                SplitView.minimumHeight: root.previewFullscreen ? 0 : editorSplit.previewMin
                SplitView.maximumHeight: root.previewFullscreen
                    ? editorSplit.height
                    : Math.max(editorSplit.previewMin, editorSplit.height * 0.72)
                // SplitView reads only the pair matching its current orientation, so both
                // sets are declared rather than swapped.
                SplitView.preferredWidth: root.previewFullscreen
                    ? editorSplit.width
                    : Math.max(editorSplit.previewMinW, editorSplit.width * 0.45)
                SplitView.minimumWidth: root.previewFullscreen ? 0 : editorSplit.previewMinW
                SplitView.maximumWidth: root.previewFullscreen
                    ? editorSplit.width
                    : Math.max(editorSplit.previewMinW, editorSplit.width * 0.65)
            }

            Item {
                id: timelinePane
                visible: !root.previewFullscreen
                SplitView.fillHeight: true
                SplitView.fillWidth: true
                SplitView.minimumHeight: editorSplit.timelineMin
                SplitView.minimumWidth: editorSplit.timelineMinW

                AndroidEditActions {
                    id: editActions
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    panel: timeline
                }

                Item {
                    id: timelineHost
                    anchors.top: editActions.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom

                    AndroidTimeline {
                        id: timeline
                        anchors.fill: parent
                        onOpenMediaRequested: root.openAssetsTab("media")
                        onOpenPropertiesRequested: root.openPropertiesSheet()
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: {
                            void EditorState.tracks
                            const tracks = EditorState.tracks
                            if (!tracks || tracks.length === 0)
                                return true
                            for (var i = 0; i < tracks.length; ++i) {
                                if (tracks[i].clips && tracks[i].clips.length > 0)
                                    return false
                            }
                            return true
                        }
                        color: Qt.rgba(Theme.appBackground.r, Theme.appBackground.g,
                                       Theme.appBackground.b, 0.82)
                        z: 5

                        Column {
                            anchors.centerIn: parent
                            spacing: Theme.spacingLg
                            width: Math.min(260, parent.width - 32)

                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: qsTr("Your timeline is empty")
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeBase
                                font.weight: Font.Medium
                            }

                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: qsTr("Import media or open the Media library to start editing.")
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                            }

                            ThemedButton {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Open Media")
                                glyph: Theme.icons.film
                                variant: "primary"
                                onClicked: root.openAssetsTab("media")
                            }
                        }
                    }
                }
            }
        }

        AndroidBottomRail {
            id: rail
            width: parent.width
            visible: !root.previewFullscreen
            onTabRequested: (tabId) => root.openAssetsTab(tabId)
            onEditRequested: root.openPropertiesSheet()
            onAddRequested: root.openAddMenu()
        }
    }

    AndroidAddMenu {
        id: addMenu
        onPicked: (tabId) => root.openAssetsTab(tabId)
    }

    AndroidBottomSheet {
        id: assetsSheet
        onClosed: {
            if (sheetKind === "assets") {
                sheetKind = ""
                rail.activeId = ""
            }
        }

        AssetsPanel {
            id: assetsPanel
            anchors.fill: parent
            sheetMode: true
            // Text, subtitles, stickers and shapes land at the playhead the moment
            // they are tapped, so the sheet has done its job and is now covering
            // the result.
            onAddCompleted: root.closeSheets()
        }
    }

    AndroidBottomSheet {
        id: propertiesSheet
        title: qsTr("Edit")
        onClosed: {
            if (sheetKind === "properties") {
                sheetKind = ""
                if (rail.activeId === "edit")
                    rail.activeId = ""
            }
        }

        PropertiesPanel {
            id: propertiesPanel
            anchors.fill: parent
            sheetMode: true
            onBrowseEffectsRequested: root.openAssetsTab("effects")
            onBrowseAudioEffectsRequested: root.openAssetsTab("sounds")
        }
    }

    ExportDialog {
        id: exportDialog
    }

    ExportProgressDialog {
        id: exportProgressDialog
        property bool dismissed: false
        onClosed: if (EditorState.exportInProgress) dismissed = true
    }

    PackageProgressDialog {
        id: packageProgressDialog
    }

    Connections {
        target: EditorState
        function onExportInProgressChanged() {
            if (EditorState.exportInProgress) {
                exportProgressDialog.dismissed = false
                exportProgressDialog.openDialog()
            }
        }
        // The crop frame is dragged on the preview, and the Settings tab it is
        // started from covers most of the screen. Cropping therefore takes the page
        // for as long as it is on, and hands it back when it ends.
        function onCanvasCropModeChanged() {
            if (EditorState.canvasCropMode)
                root.closeSheets()
            root.previewFullscreen = EditorState.canvasCropMode
        }
        // Selection must not auto-open the Edit sheet: that ran on press (before
        // release), stole the gesture so clips could not drag, and blocked long-press
        // menus. Open via the Edit rail; an already-open sheet still updates via
        // PropertiesPanel bindings.
        function onSaveRequested() { root.saveProject() }
        function onOpenRequested() { root.openProject() }
        function onNewProjectRequested() { root.requestNewProject() }
    }

    Component.onCompleted: AssetLibrary.ensureAllMedia()
}

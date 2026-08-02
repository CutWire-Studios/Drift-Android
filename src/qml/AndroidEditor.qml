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

    readonly property var projectFilter: [qsTr("Drift project (*.drift)")]

    function openAssetsTab(tabId) {
        sheetKind = "assets"
        rail.activeId = tabId
        assetsPanel.showTab(tabId)
        assetsSheet.title = assetsPanel.tabLabel(tabId)
        if (!assetsSheet.opened)
            assetsSheet.open()
        if (propertiesSheet.opened)
            propertiesSheet.close()
    }

    function openPropertiesSheet() {
        sheetKind = "properties"
        rail.activeId = "edit"
        if (!propertiesSheet.opened)
            propertiesSheet.open()
        if (assetsSheet.opened)
            assetsSheet.close()
    }

    function closeSheets() {
        sheetKind = ""
        rail.activeId = ""
        if (assetsSheet.opened)
            assetsSheet.close()
        if (propertiesSheet.opened)
            propertiesSheet.close()
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
            onBackRequested: root.backRequested()
            onExportRequested: exportDialog.openDialog()
            onSaveRequested: root.saveProject()
            onPackageRequested: root.packageProject()
            onOpenRequested: root.openProject()
            onNewRequested: root.requestNewProject()
            onLayoutRequested: layoutChooser.openFromSettings()
        }

        SplitView {
            id: editorSplit
            width: parent.width
            height: Math.max(0, parent.height - topBar.height - rail.height)
            orientation: Qt.Vertical

            handle: Item {
                implicitWidth: editorSplit.width
                implicitHeight: Theme.androidSplitterHeight

                Rectangle {
                    anchors.fill: parent
                    color: Theme.panelBackground
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 4
                    radius: 2
                    color: T.SplitHandle.pressed ? Theme.primary : Theme.panelBorder
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    cursorShape: Qt.SplitVCursor
                }
            }

            AndroidPreview {
                id: preview
                // Avoid fixed mins larger than the first layout pass (height may be 0).
                SplitView.preferredHeight: Math.min(
                    implicitHeight,
                    Math.max(SplitView.minimumHeight, editorSplit.height * 0.42))
                SplitView.minimumHeight: Math.min(
                    editorSplit.height,
                    Theme.androidPreviewTransportHeight + 72)
                SplitView.maximumHeight: Math.max(
                    SplitView.minimumHeight,
                    editorSplit.height * 0.72)
            }

            Item {
                id: timelinePane
                SplitView.fillHeight: true
                SplitView.minimumHeight: Math.min(
                    editorSplit.height,
                    Theme.androidEditActionsHeight + 120)

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
            onTabRequested: (tabId) => root.openAssetsTab(tabId)
            onEditRequested: root.openPropertiesSheet()
        }
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

    LayoutChooserDialog {
        id: layoutChooser
    }

    Connections {
        target: EditorState
        function onExportInProgressChanged() {
            if (EditorState.exportInProgress) {
                exportProgressDialog.dismissed = false
                exportProgressDialog.openDialog()
            }
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

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"

// Android entry shell: home (layout + recent + import) → CapCut editor.
ApplicationWindow {
    id: window
    visible: true
    color: Theme.appBackground
    title: "CutWire Drift"

    readonly property var projectFilter: [qsTr("Drift project (*.drift)")]
    property bool inEditor: false
    property bool forceClose: false
    property var _pendingAfterUnsaved: null

    function confirmIfDirty(action) {
        if (!EditorState.hasUnsavedChanges) {
            action()
            return
        }
        window._pendingAfterUnsaved = action
        unsavedDialog.openDialog()
    }

    function saveProject() {
        if (EditorState.currentProjectPath && EditorState.currentProjectPath.length > 0) {
            EditorState.saveProject(EditorState.fileUrl(EditorState.currentProjectPath))
            return !EditorState.hasUnsavedChanges
        }
        const url = FileDialogs.saveFile(qsTr("Save Project"), window.projectFilter,
                                         EditorState.projectName, "drift")
        if (url === "")
            return false
        EditorState.saveProject(url)
        return !EditorState.hasUnsavedChanges
    }

    function configureAndAddAsset(assetIndex, runner) {
        if (!EditorState.shouldConfigureProjectForAsset(assetIndex)) {
            runner()
            return
        }
        projectSetupDialog.openForAsset(assetIndex, runner)
    }

    function openLayoutChooser() {
        layoutChooserDialog.openFromSettings()
    }

    function showHome() {
        if (!window.inEditor)
            return
        if (stack.depth > 1)
            stack.pop()
        window.inEditor = false
    }

    function showEditor() {
        if (window.inEditor)
            return
        stack.push(editorComponent)
        window.inEditor = true
    }

    function goBack() {
        if (window.inEditor) {
            confirmIfDirty(function () {
                window.showHome()
            })
            return
        }
        if (EditorState.hasUnsavedChanges) {
            confirmIfDirty(function () {
                EditorState.discardUnsavedChanges()
                window.forceClose = true
                window.close()
            })
        } else {
            window.forceClose = true
            window.close()
        }
    }

    function openProjectFile() {
        confirmIfDirty(function () {
            const url = FileDialogs.openFile(qsTr("Open Project"), window.projectFilter)
            if (url !== "") {
                EditorState.loadProject(url)
                showEditor()
            }
        })
    }

    function openRecent(path) {
        confirmIfDirty(function () {
            EditorState.openRecentProject(path)
            showEditor()
        })
    }

    onClosing: function (close) {
        if (window.forceClose || !EditorState.hasUnsavedChanges)
            return
        close.accepted = false
        confirmIfDirty(function () {
            EditorState.discardUnsavedChanges()
            window.forceClose = true
            window.close()
        })
    }

    Shortcut {
        sequences: [StandardKey.Back, "Esc"]
        onActivated: window.goBack()
    }

    ProjectSetupDialog { id: projectSetupDialog }
    LayoutChooserDialog { id: layoutChooserDialog }

    RecoveryDialog {
        id: recoveryDialog
        onAccepted: Qt.callLater(window.showEditor)
    }

    UnsavedChangesDialog {
        id: unsavedDialog
        onSaveChosen: {
            if (!window.saveProject())
                return
            const action = window._pendingAfterUnsaved
            window._pendingAfterUnsaved = null
            close()
            if (action)
                action()
        }
        onDiscardChosen: {
            const action = window._pendingAfterUnsaved
            window._pendingAfterUnsaved = null
            close()
            EditorState.discardUnsavedChanges()
            if (action)
                action()
        }
        onRejected: window._pendingAfterUnsaved = null
    }

    AddonManagerDialog { id: addonManagerDialog }
    MissingAddonsDialog { id: missingAddonsDialog }

    function openAddonManager(kind) {
        if (kind === undefined)
            addonManagerDialog.open()
        else
            addonManagerDialog.openForKind(kind)
    }

    Timer {
        id: recoveryOpenTimer
        interval: 150
        repeat: true
        property int attempts: 0
        onTriggered: {
            if (!EditorState.recoveryAvailable) {
                stop()
                attempts = 0
                return
            }
            if (!recoveryDialog.visible)
                recoveryDialog.open()
            if (recoveryDialog.visible || ++attempts >= 20)
                stop()
        }
    }

    Component.onCompleted: {
        if (EditorState.recoveryAvailable)
            recoveryOpenTimer.start()
    }

    Connections {
        target: EditorState
        function onRecoveryChanged() {
            if (EditorState.recoveryAvailable)
                recoveryOpenTimer.start()
        }
        function onMissingAddons(addons) {
            missingAddonsDialog.openFor(addons)
        }
        function onExportFinished(success) {
            if (success) {
                Toasts.success(qsTr("Export finished."))
                return
            }
            if (EditorState.lastMessage === "Export cancelled")
                Toasts.info(qsTr("Export cancelled."))
            else
                Toasts.error(qsTr("Export failed. Check the save location and free space."))
        }
        function onPackageFinished(ok, message) {
            if (ok)
                Toasts.success(message)
            else
                Toasts.error(qsTr("Couldn't create the shareable copy: %1").arg(message))
        }
        function onLastMessageChanged() {
            const message = EditorState.lastMessage
            if (message.length === 0)
                return
            if (/fail|error|could not|unable|invalid|denied/i.test(message))
                Toasts.error(message)
            else
                Toasts.info(message)
        }
        function onTransformBlocked(reason) {
            Toasts.warning(reason)
        }
    }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: homeComponent
    }

    Component {
        id: homeComponent
        AndroidHome {
            onEnterEditor: window.showEditor()
            onOpenProjectRequested: window.openProjectFile()
            onOpenRecentRequested: (path) => window.openRecent(path)
        }
    }

    Component {
        id: editorComponent
        AndroidEditor {
            onBackRequested: window.goBack()
            onGoHomeRequested: {
                window.inEditor = false
                if (stack.depth > 1)
                    stack.pop()
            }
        }
    }

    ToastHost { }
}

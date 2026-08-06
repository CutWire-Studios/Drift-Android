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
    // The live AndroidEditor instance, so Back can ask it to close a sheet first.
    property var editorPage: null

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
        // Opting in to reopening the last project means the autosave is restored on the
        // next launch, so asking to save on the way out is a question already answered.
        if (window.forceClose || !EditorState.hasUnsavedChanges || EditorState.reopenLastProject)
            return
        close.accepted = false
        confirmIfDirty(function () {
            EditorState.discardUnsavedChanges()
            window.forceClose = true
            window.close()
        })
    }

    // Android's system Back. Order matters: a full-screen clip tool, then any open
    // sheet/dialog, and only then the home/exit step — otherwise Back walked out of the
    // editor from behind a modal that stayed on screen.
    // Application context, not the default window context: every tool window below calls
    // requestActivate() and so becomes the active window, which left a WindowShortcut on
    // this window dead exactly when the toolWindowOpen branch was needed.
    Shortcut {
        sequences: [StandardKey.Back, "Esc"]
        context: Qt.ApplicationShortcut
        onActivated: {
            if (window.toolWindowOpen) {
                window.closeTopToolWindow()
                return
            }
            if (window.closeTopModal())
                return
            if (window.editorPage && window.editorPage.handleBack())
                return
            window.goBack()
        }
    }

    // Modals hosted here. They close on Escape, which Android's Back key is not, so
    // without this Back left them on screen and walked out of the editor behind them.
    // Ordered by how they stack: newest-opened first.
    function closeTopModal() {
        const modals = [projectSetupDialog, layoutChooserDialog, projectPropertiesDialog,
                        recoveryDialog, unsavedDialog, addonStartupDialog, addonManagerDialog,
                        missingAddonsDialog, updateDialog, reverseProgressDialog,
                        subtitleProgressDialog]
        for (var i = 0; i < modals.length; ++i) {
            if (modals[i] && modals[i].visible) {
                modals[i].close()
                return true
            }
        }
        return false
    }

    ProjectSetupDialog { id: projectSetupDialog }
    LayoutChooserDialog { id: layoutChooserDialog }

    // Desktop reaches this from EditorHeader, which Android replaces with AndroidTopBar;
    // hosting it here is what gives that bar's overflow something to open.
    ProjectPropertiesDialog { id: projectPropertiesDialog }

    function openProjectProperties() {
        projectPropertiesDialog.openDialog()
    }

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
    AddonStartupDialog { id: addonStartupDialog }
    MissingAddonsDialog { id: missingAddonsDialog }
    UpdateDialog { id: updateDialog }
    SubtitleProgressDialog { id: subtitleProgressDialog }
    ReverseProgressDialog { id: reverseProgressDialog }

    // Long-running clip tools. These are top-level Windows on desktop; on Android the
    // platform gives each one the whole screen, so they read as full-screen pages that
    // the system Back key dismisses (see the Back shortcut above).
    SegmentationWindow { id: segmentationWindow }
    DenoiseWindow { id: denoiseWindow }
    SpeedCurveWindow { id: speedCurveWindow }
    FadeCurveWindow { id: fadeCurveWindow }

    // Every inspector reaches these through Window.window.<name>() — the same contract
    // Main.qml offers on desktop. A missing one is a runtime TypeError, not a dead button.
    function openSegmentation(track, clip, startSeconds, durationSeconds) {
        segmentationWindow.openFor(track, clip, startSeconds, durationSeconds)
    }

    function openDenoise(track, clip, durationSeconds) {
        denoiseWindow.openFor(track, clip, durationSeconds)
    }

    function openSpeedCurve(track, clip) {
        speedCurveWindow.openFor(track, clip)
    }

    function openFadeCurve(track, clip) {
        fadeCurveWindow.openFor(track, clip)
    }

    function openAddonManager(kind) {
        if (kind === undefined)
            addonManagerDialog.open()
        else
            addonManagerDialog.openForKind(kind)
    }

    function openExtras() {
        if (addonStartupDialog.openForAttention())
            return
        openAddonManager()
    }

    function openUpdateDialog() {
        updateDialog.open()
    }

    readonly property alias addonAttentionNeeded: addonStartupDialog.needsAttention

    function refreshAddonAttention() {
        addonStartupDialog.refreshAttention()
    }

    // True while any of the full-screen clip tools owns the display, so the editor's
    // Back handling defers to them instead of popping the stack behind them.
    readonly property bool toolWindowOpen: segmentationWindow.visible || denoiseWindow.visible
                                           || speedCurveWindow.visible || fadeCurveWindow.visible

    function closeTopToolWindow() {
        if (segmentationWindow.visible)
            segmentationWindow.close()
        else if (denoiseWindow.visible)
            denoiseWindow.close()
        else if (speedCurveWindow.visible)
            speedCurveWindow.close()
        else if (fadeCurveWindow.visible)
            fadeCurveWindow.close()
    }

    Timer {
        id: recoveryOpenTimer
        interval: 150
        repeat: true
        property int attempts: 0
        onTriggered: {
            // Opting in to reopening the last project already restores the autosave, so
            // asking about it as well is a question the user has answered once already.
            if (!EditorState.recoveryAvailable || EditorState.reopenLastProject) {
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
        // Tapping a .drift in a file manager launches us with ACTION_VIEW. That project is
        // what the user asked for, so it outranks the reopen-last-project restore below.
        // Empty on desktop, where the intent does not exist.
        const launched = FileDialogs.takeLaunchUrl()
        if (launched !== "") {
            window.confirmIfDirty(function () {
                EditorState.loadProject(launched)
                Qt.callLater(window.showEditor)
            })
            return
        }
        // "Reopen last project" restores the autosave or the last clean .drift silently and
        // lands straight in the editor; the home page would be a step backwards from it.
        if (EditorState.restoreLastSessionIfEnabled()) {
            Qt.callLater(window.showEditor)
            return
        }
        if (EditorState.recoveryAvailable)
            recoveryOpenTimer.start()
        else
            Qt.callLater(window.refreshAddonAttention)
    }

    Connections {
        target: EditorState
        function onRecoveryChanged() {
            if (EditorState.reopenLastProject)
                return
            if (EditorState.recoveryAvailable)
                recoveryOpenTimer.start()
        }
        function onOpenSegmentationWindowRequested(track, clip, startSeconds, durationSeconds) {
            segmentationWindow.openFor(track, clip, startSeconds, durationSeconds, true)
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
        function onSubtitleGenerationFinished(ok, message) {
            if (ok)
                Toasts.success(message.length > 0 ? message : qsTr("Captions created."))
            else
                Toasts.error(message.length > 0
                             ? qsTr("Couldn’t create captions: %1").arg(message)
                             : qsTr("Couldn’t create captions."))
        }
        // Severity comes from the backend. Inferring it by regexing the prose matched
        // none of the real failure strings, so a corrupt-project open read as a neutral
        // info toast that auto-dismissed like "Project saved".
        function onLastMessageChanged() {
            const message = EditorState.lastMessage
            if (message.length === 0)
                return
            switch (EditorState.lastMessageSeverity) {
            case "error":   Toasts.error(message); break
            case "warning": Toasts.warning(message); break
            case "success": Toasts.success(message); break
            default:        Toasts.info(message); break
            }
        }
        function onTransformBlocked(reason) {
            Toasts.warning(reason)
        }
    }

    // Backgrounding the app leaves the audio sink and the composite timer running, which
    // keeps decoding video nobody can see and holds the audio focus. Android will kill the
    // process for it eventually; pausing is the honest response to losing the foreground.
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive)
                return
            if (EditorState.playing)
                EditorState.playback.pause()
            // Losing the foreground is the last moment guaranteed to run: the OS can reclaim the
            // process from here without another callback, and aboutToQuit does not fire when it does.
            EditorState.flushRecoverySnapshot()
        }
    }

    Connections {
        target: Addons
        function onRefreshingChanged() {
            if (!Addons.refreshing)
                Qt.callLater(window.refreshAddonAttention)
        }
        function onCatalogChanged() { Qt.callLater(window.refreshAddonAttention) }
        function onRemindEssentialChanged() { Qt.callLater(window.refreshAddonAttention) }
        function onRemindUpdatesChanged() { Qt.callLater(window.refreshAddonAttention) }

        // A download or signature failure only reaches the addon manager's status line,
        // so a pack that failed while that dialog was closed failed silently.
        function onTransferFailed(id, reason) {
            if (reason === "Cancelled")
                return
            Toasts.error(qsTr("Couldn’t install “%1”: %2").arg(id).arg(reason))
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
            Component.onCompleted: window.editorPage = this
            Component.onDestruction: if (window.editorPage === this) window.editorPage = null
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

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"

// Combined start page: recent projects + layout picker + import / blank CTAs.
Item {
    id: root

    signal enterEditor()
    signal openProjectRequested()
    signal openRecentRequested(string path)

    readonly property var mediaFilter: [
        qsTr("Media files (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mp3 *.wav *.aac *.flac *.ogg *.m4a *.png *.jpg *.jpeg *.gif *.webp *.bmp)")
    ]

    readonly property bool needsAttention: {
        const win = root.Window.window
        return (win ? win.addonAttentionNeeded : false) || Updates.updateAvailable
    }

    function applyLayoutAndPrepare() {
        EditorState.newProject()
        layoutPicker.apply()
    }

    // Bookkeeping for the async import below. `countBefore` is captured before the copy
    // starts so the completion handler knows which rows are new. `importOwned` exists
    // because StackView keeps this page alive underneath the editor: without it, an
    // import started from the editor's Media tab would also land here and shove the user
    // back into the editor with a second copy of the clips.
    property bool importOwned: false
    property int countBefore: 0
    property int importDone: 0
    property int importTotal: 0
    property string importName: ""

    function importAndEdit() {
        applyLayoutAndPrepare()
        const urls = FileDialogs.openFiles(qsTr("Import Media"), root.mediaFilter)
        if (!urls || urls.length === 0) {
            // User cancelled the picker — still enter the blank project they just created.
            root.enterEditor()
            return
        }
        // A picked file is a content:// document, and reading one means copying it out of
        // the SAF stream first. Doing that inline froze the whole app for as long as the
        // copy took — minutes for a few 4K clips, long past the point Android offers to
        // kill it. The copy runs off-thread now and this screen shows its progress.
        const before = AssetLibrary.count
        if (!AssetLibrary.importUrlsAsync(urls)) {
            Toasts.warning(qsTr("An import is already running."))
            return
        }
        root.importOwned = true
        root.countBefore = before
        root.importDone = 0
        root.importTotal = urls.length
        root.importName = ""
    }

    Connections {
        target: AssetLibrary
        function onImportProgress(done, total, name) {
            if (!root.importOwned)
                return
            root.importDone = done
            root.importTotal = total
            root.importName = name
        }
        function onImportFinished(materialized, failed) {
            if (!root.importOwned)
                return
            root.importOwned = false
            for (var i = root.countBefore; i < AssetLibrary.count; ++i)
                EditorState.addClipFromAsset(i)
            const added = AssetLibrary.count - root.countBefore
            if (added > 0)
                Toasts.success(qsTr("Imported %n file(s).", "", added))
            else
                Toasts.error(qsTr("Could not import the selected file(s)."))
            root.enterEditor()
        }
    }

    function startBlank() {
        applyLayoutAndPrepare()
        root.enterEditor()
    }

    Component.onCompleted: layoutPicker.resetForMobile()

    Flickable {
        id: flick
        anchors.fill: parent
        // The home page owns the whole window, so it is the one that has to clear the
        // status bar, the gesture pill and any landscape cutout itself.
        anchors.topMargin: root.SafeArea.margins.top
        anchors.bottomMargin: Theme.spacingXl + root.SafeArea.margins.bottom
        anchors.leftMargin: root.SafeArea.margins.left
        anchors.rightMargin: root.SafeArea.margins.right
        contentWidth: width
        contentHeight: pageColumn.implicitHeight + Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Column {
            id: pageColumn
            width: parent.width
            spacing: Theme.spacing2xl
            topPadding: Theme.spacing2xl
            leftPadding: Theme.pagePadding
            rightPadding: Theme.pagePadding

            // --- Header -------------------------------------------------------
            Item {
                width: parent.width - parent.leftPadding - parent.rightPadding
                height: Math.max(brandCol.height, headerActions.height)

                Column {
                    id: brandCol
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        text: "Drift"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }

                    Text {
                        text: qsTr("Create polished videos fast")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                }

                Row {
                    id: headerActions
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingSm

                    ThemedButton {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Open")
                        glyph: Theme.icons.folder
                        variant: "secondary"
                        onClicked: root.openProjectRequested()
                    }

                    // The theme toggle, the addon manager and the update prompt used to live
                    // only in the editor's overflow, so a first run — which has no project yet
                    // — could not reach the packs the editor needs to be usable at all.
                    Item {
                        width: Theme.androidIconButtonSize
                        height: Theme.androidIconButtonSize
                        anchors.verticalCenter: parent.verticalCenter

                        IconButton {
                            anchors.fill: parent
                            buttonSize: Theme.androidIconButtonSize
                            iconSize: Theme.iconSizeLg
                            glyph: Theme.icons.sliders
                            variant: "text"
                            tooltip: qsTr("More")
                            onClicked: homeMenu.popup()
                        }

                        // The editor's Extras button pulses; a badge is the phone-sized
                        // version of the same "something needs you" signal.
                        Rectangle {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 4
                            width: 8
                            height: 8
                            radius: 4
                            visible: root.needsAttention
                            color: Theme.destructive
                        }

                        ThemedContextMenu {
                            id: homeMenu

                            ThemedMenuItem {
                                text: Theme.darkMode ? qsTr("Light mode") : qsTr("Dark mode")
                                icon.name: Theme.darkMode ? Theme.icons.sun : Theme.icons.moon
                                onTriggered: Theme.toggleDarkMode()
                            }
                            ThemedMenuItem {
                                text: qsTr("Extras")
                                icon.name: Theme.icons.package
                                onTriggered: Window.window.openExtras()
                            }
                            ThemedMenuItem {
                                text: qsTr("Update available")
                                icon.name: Theme.icons.download
                                visible: Updates.updateAvailable
                                onTriggered: Window.window.openUpdateDialog()
                            }
                        }
                    }
                }
            }

            // --- Recent projects ----------------------------------------------
            Column {
                width: parent.width - parent.leftPadding - parent.rightPadding
                spacing: Theme.spacingMd
                visible: EditorState.recentProjects.length > 0

                ThemedLabel {
                    text: qsTr("Recent projects")
                    tone: "default"
                    size: "sm"
                }

                Flickable {
                    width: parent.width
                    height: Theme.androidHomeRecentCardHeight
                    contentWidth: recentRow.width
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick

                    Row {
                        id: recentRow
                        spacing: Theme.spacingMd
                        height: parent.height

                        Repeater {
                            model: EditorState.recentProjects

                            delegate: Rectangle {
                                id: card
                                required property var modelData
                                width: Theme.androidHomeRecentCardWidth
                                height: Theme.androidHomeRecentCardHeight
                                radius: Theme.radiusMd
                                color: Theme.panelBackground
                                border.width: Theme.borderWidth
                                border.color: Theme.panelBorder
                                opacity: modelData.exists === false ? 0.55 : 1

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingLg
                                    spacing: Theme.spacingSm

                                    Rectangle {
                                        width: parent.width
                                        height: 32
                                        radius: Theme.radiusSm
                                        color: Theme.panelAccent

                                        IconGlyph {
                                            anchors.centerIn: parent
                                            glyph: Theme.icons.film
                                            iconSize: 18
                                            iconColor: Theme.mutedForeground
                                        }

                                        // Same on-disk signal the desktop recents list carries;
                                        // the card's dimming alone did not say what was wrong.
                                        Rectangle {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.margins: 4
                                            width: 8
                                            height: 8
                                            radius: 4
                                            color: card.modelData.exists === false
                                                   ? Theme.mutedForeground : Theme.constructive
                                        }
                                    }

                                    Text {
                                        width: parent.width
                                        text: {
                                            const n = card.modelData.name || ""
                                            return n.replace(/\.drift$/i, "") || qsTr("Untitled")
                                        }
                                        color: Theme.panelForeground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeXs
                                        font.weight: Font.Medium
                                        elide: Text.ElideMiddle
                                    }

                                    // Elided from the left: on a 140px card the tail — the folder
                                    // and file name — is the half that tells two projects apart.
                                    Text {
                                        width: parent.width
                                        text: card.modelData.path
                                        color: Theme.mutedForeground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeXs
                                        elide: Text.ElideLeft
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    pressAndHoldInterval: 450
                                    // Long-press stands in for the right click the phone
                                    // does not have.
                                    property bool heldMenu: false
                                    onPressed: heldMenu = false
                                    onPressAndHold: {
                                        heldMenu = true
                                        cardMenu.popup()
                                    }
                                    onClicked: {
                                        if (heldMenu)
                                            return
                                        if (card.modelData.exists === false) {
                                            Toasts.warning(qsTr("That project file is missing."))
                                            return
                                        }
                                        root.openRecentRequested(card.modelData.path)
                                    }
                                }

                                ThemedContextMenu {
                                    id: cardMenu

                                    ThemedMenuItem {
                                        text: qsTr("Remove from recents")
                                        icon.name: Theme.icons.trash
                                        onTriggered: EditorState.removeRecentProject(card.modelData.path)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // --- Layout picker ------------------------------------------------
            Column {
                width: parent.width - parent.leftPadding - parent.rightPadding
                spacing: Theme.spacingMd

                ThemedLabel {
                    text: qsTr("New project")
                    tone: "default"
                    size: "sm"
                }

                AndroidLayoutPicker {
                    id: layoutPicker
                    width: parent.width
                    compact: true
                }
            }

            // --- CTAs ---------------------------------------------------------
            Column {
                width: parent.width - parent.leftPadding - parent.rightPadding
                spacing: Theme.spacingMd

                ThemedButton {
                    width: parent.width
                    text: qsTr("Import media & edit")
                    glyph: Theme.icons.upload
                    variant: "primary"
                    enabled: !AssetLibrary.importing
                    onClicked: root.importAndEdit()
                }

                ThemedButton {
                    width: parent.width
                    text: qsTr("Start blank")
                    glyph: Theme.icons.plus
                    variant: "secondary"
                    enabled: !AssetLibrary.importing
                    onClicked: root.startBlank()
                }
            }
        }
    }

    // Copying a picked document out of the SAF stream is the one part of import that
    // takes real time, so it gets a real progress surface rather than a frozen screen.
    Rectangle {
        anchors.fill: parent
        visible: root.importOwned && AssetLibrary.importing
        color: Qt.rgba(Theme.appBackground.r, Theme.appBackground.g, Theme.appBackground.b, 0.92)

        // Swallow taps so nothing behind the overlay can be started mid-copy.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Column {
            anchors.centerIn: parent
            width: Math.min(280, parent.width - Theme.spacing3xl * 2)
            spacing: Theme.spacingLg

            CircularProgress {
                anchors.horizontalCenter: parent.horizontalCenter
                size: 48
                strokeWidth: 4
                indeterminate: root.importTotal <= 0
                value: root.importTotal > 0 ? root.importDone / root.importTotal : 0
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: root.importTotal > 1
                      ? qsTr("Importing %1 of %2…").arg(root.importDone + 1).arg(root.importTotal)
                      : qsTr("Importing…")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.Medium
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 2
                elide: Text.ElideMiddle
                visible: root.importName.length > 0
                text: root.importName
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
        }
    }
}

import QtQuick
import QtQuick.Controls.Basic
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

    function applyLayoutAndPrepare() {
        EditorState.newProject()
        layoutPicker.apply()
    }

    function importAndEdit() {
        applyLayoutAndPrepare()
        const urls = FileDialogs.openFiles(qsTr("Import Media"), root.mediaFilter)
        if (!urls || urls.length === 0) {
            // User cancelled the picker — still enter the blank project they just created.
            root.enterEditor()
            return
        }
        const before = AssetLibrary.count
        AssetLibrary.importUrls(urls)
        for (var i = before; i < AssetLibrary.count; ++i)
            EditorState.addClipFromAsset(i)
        const added = AssetLibrary.count - before
        if (added > 0)
            Toasts.success(qsTr("Imported %n file(s).", "", added))
        else
            Toasts.error(qsTr("Could not import the selected file(s)."))
        root.enterEditor()
    }

    function startBlank() {
        applyLayoutAndPrepare()
        root.enterEditor()
    }

    Component.onCompleted: layoutPicker.resetForMobile()

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.bottomMargin: Theme.spacingXl
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
                height: Math.max(brandCol.height, openBtn.height)

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

                ThemedButton {
                    id: openBtn
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Open")
                    glyph: Theme.icons.folder
                    variant: "secondary"
                    onClicked: root.openProjectRequested()
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
                                        height: 40
                                        radius: Theme.radiusSm
                                        color: Theme.panelAccent

                                        IconGlyph {
                                            anchors.centerIn: parent
                                            glyph: Theme.icons.film
                                            iconSize: 20
                                            iconColor: Theme.mutedForeground
                                        }
                                    }

                                    Text {
                                        width: parent.width
                                        text: {
                                            const n = modelData.name || ""
                                            return n.replace(/\.drift$/i, "") || qsTr("Untitled")
                                        }
                                        color: Theme.panelForeground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeXs
                                        font.weight: Font.Medium
                                        elide: Text.ElideMiddle
                                        maximumLineCount: 2
                                        wrapMode: Text.Wrap
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        if (modelData.exists === false) {
                                            Toasts.warning(qsTr("That project file is missing."))
                                            return
                                        }
                                        root.openRecentRequested(modelData.path)
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
                    onClicked: root.importAndEdit()
                }

                ThemedButton {
                    width: parent.width
                    text: qsTr("Start blank")
                    glyph: Theme.icons.plus
                    variant: "secondary"
                    onClicked: root.startBlank()
                }
            }
        }
    }
}

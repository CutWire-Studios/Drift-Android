import QtQuick
import QtQuick.Controls.Basic
import Drift

// Shown on a fresh empty project so the user picks a platform canvas before editing.
// Category chips → template list (icon + label). "Decide later" keeps the default size
// and marks the layout chosen so neither this dialog nor ProjectSetupDialog reappear
// for the current project (New Project clears that and asks again).
ThemedDialog {
    id: root

    title: qsTr("Choose your video layout")
    acceptText: qsTr("Continue")
    rejectVariant: "secondary"
    preferredWidth: Theme.dialogWidthLg

    property string activeCategory: "youtube"
    property string templateId: "yt_video"
    property string qualityId: "1080p"
    // First-launch uses "Decide later"; Settings reopen uses "Cancel".
    property bool fromSettings: false

    rejectText: fromSettings ? qsTr("Cancel") : qsTr("Decide later")
    closePolicy: fromSettings ? Popup.CloseOnEscape | Popup.CloseOnPressOutside : Popup.NoAutoClose

    readonly property var categories: [
        { id: "youtube", label: qsTr("YouTube"), icon: Theme.icons.brandYoutube },
        { id: "instagram", label: qsTr("Instagram"), icon: Theme.icons.brandInstagram },
        { id: "facebook", label: qsTr("Facebook"), icon: Theme.icons.brandFacebook },
        { id: "tiktok", label: qsTr("TikTok"), icon: Theme.icons.brandTiktok },
        { id: "more", label: qsTr("More"), icon: Theme.icons.grid }
    ]

    // aspect: "9:16" | "16:9" | "1:1" | "4:5"
    readonly property var templates: [
        { id: "yt_video", category: "youtube", label: qsTr("YT Video"), detail: "16:9", aspect: "16:9", icon: Theme.icons.brandYoutube },
        { id: "yt_short", category: "youtube", label: qsTr("YT Short"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandYoutube },
        { id: "ig_reel", category: "instagram", label: qsTr("IG Reel"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandInstagram },
        { id: "ig_story", category: "instagram", label: qsTr("IG Story"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandInstagram },
        { id: "ig_post", category: "instagram", label: qsTr("IG Post"), detail: "1:1", aspect: "1:1", icon: Theme.icons.brandInstagram },
        { id: "ig_feed", category: "instagram", label: qsTr("IG Feed"), detail: "4:5", aspect: "4:5", icon: Theme.icons.brandInstagram },
        { id: "fb_reel", category: "facebook", label: qsTr("FB Reel"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandFacebook },
        { id: "fb_video", category: "facebook", label: qsTr("FB Video"), detail: "16:9", aspect: "16:9", icon: Theme.icons.brandFacebook },
        { id: "fb_story", category: "facebook", label: qsTr("FB Story"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandFacebook },
        { id: "tiktok", category: "tiktok", label: qsTr("TikTok"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandTiktok },
        { id: "snapchat", category: "more", label: qsTr("Snapchat"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandSnapchat },
        { id: "x_video", category: "more", label: qsTr("X / Twitter"), detail: "16:9", aspect: "16:9", icon: Theme.icons.brandX },
        { id: "linkedin", category: "more", label: qsTr("LinkedIn"), detail: "16:9", aspect: "16:9", icon: Theme.icons.brandLinkedin },
        { id: "square", category: "more", label: qsTr("Square"), detail: "1:1", aspect: "1:1", icon: Theme.icons.square },
        { id: "landscape", category: "more", label: qsTr("Landscape"), detail: "16:9", aspect: "16:9", icon: Theme.icons.monitor },
        { id: "portrait", category: "more", label: qsTr("Portrait"), detail: "9:16", aspect: "9:16", icon: Theme.icons.smartphone }
    ]

    readonly property var qualities: [
        { id: "4k", label: qsTr("4K") },
        { id: "1080p", label: qsTr("1080p") },
        { id: "720p", label: qsTr("720p") }
    ]

    readonly property var categoryTemplates: {
        const out = []
        for (let i = 0; i < templates.length; ++i) {
            if (templates[i].category === activeCategory)
                out.push(templates[i])
        }
        return out
    }

    readonly property var selectedTemplate: {
        for (let i = 0; i < templates.length; ++i) {
            if (templates[i].id === templateId)
                return templates[i]
        }
        return templates[0]
    }

    readonly property string aspect: selectedTemplate.aspect || "16:9"

    readonly property int qualityEdge: {
        if (qualityId === "4k") return 2160
        if (qualityId === "720p") return 720
        return 1080
    }

    readonly property int outWidth: {
        switch (aspect) {
        case "9:16": return qualityEdge
        case "4:5": return qualityEdge
        case "1:1": return qualityEdge
        case "16:9":
        default:
            if (qualityId === "4k") return 3840
            if (qualityId === "720p") return 1280
            return 1920
        }
    }

    readonly property int outHeight: {
        switch (aspect) {
        case "9:16":
            if (qualityId === "4k") return 3840
            if (qualityId === "720p") return 1280
            return 1920
        case "4:5":
            return Math.round(outWidth * 5 / 4)
        case "1:1":
            return outWidth
        case "16:9":
        default:
            if (qualityId === "4k") return 2160
            if (qualityId === "720p") return 720
            return 1080
        }
    }

    readonly property real aspectBoxSize: 88
    readonly property real aspectScale: {
        const w = Math.max(1, outWidth)
        const h = Math.max(1, outHeight)
        return Math.min(aspectBoxSize / w, aspectBoxSize / h)
    }
    readonly property real previewW: Math.max(8, outWidth * aspectScale)
    readonly property real previewH: Math.max(8, outHeight * aspectScale)

    function selectCategory(categoryId) {
        activeCategory = categoryId
        if (selectedTemplate.category === categoryId)
            return
        for (let i = 0; i < templates.length; ++i) {
            if (templates[i].category === categoryId) {
                templateId = templates[i].id
                return
            }
        }
    }

    function resetSelection() {
        activeCategory = "youtube"
        templateId = "yt_video"
        qualityId = "1080p"
    }

    function matchCurrentProject() {
        const w = EditorState.projectWidth()
        const h = EditorState.projectHeight()
        let bestId = "yt_video"
        let bestCat = "youtube"
        let bestQuality = "1080p"
        let bestScore = Number.MAX_VALUE

        const qualities = [
            { id: "4k", edge: 2160 },
            { id: "1080p", edge: 1080 },
            { id: "720p", edge: 720 }
        ]

        for (let i = 0; i < templates.length; ++i) {
            const t = templates[i]
            for (let q = 0; q < qualities.length; ++q) {
                const edge = qualities[q].edge
                let tw = 0
                let th = 0
                switch (t.aspect) {
                case "9:16":
                    tw = edge
                    th = qualities[q].id === "4k" ? 3840 : (qualities[q].id === "720p" ? 1280 : 1920)
                    break
                case "4:5":
                    tw = edge
                    th = Math.round(edge * 5 / 4)
                    break
                case "1:1":
                    tw = edge
                    th = edge
                    break
                default:
                    tw = qualities[q].id === "4k" ? 3840 : (qualities[q].id === "720p" ? 1280 : 1920)
                    th = qualities[q].id === "4k" ? 2160 : (qualities[q].id === "720p" ? 720 : 1080)
                    break
                }
                const score = Math.abs(tw - w) + Math.abs(th - h)
                if (score < bestScore) {
                    bestScore = score
                    bestId = t.id
                    bestCat = t.category
                    bestQuality = qualities[q].id
                }
            }
        }

        templateId = bestId
        activeCategory = bestCat
        qualityId = bestQuality
    }

    function openChooser() {
        fromSettings = false
        resetSelection()
        open()
    }

    function openFromSettings() {
        fromSettings = true
        matchCurrentProject()
        open()
    }

    onAccepted: {
        EditorState.setProjectSetup(outWidth, outHeight, EditorState.projectFps())
        EditorState.markProjectLayoutChosen()
    }

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : 600

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: fromSettings
                  ? qsTr("Pick a platform template and quality. This updates the project video size.")
                  : qsTr("Pick a category, then a template and quality. You can change this anytime in Settings → Choose layout.")
        }

        Flickable {
            id: categoryFlick
            width: parent.width
            height: 34
            contentWidth: categoryRow.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: categoryRow
                height: parent.height
                spacing: 6

                Repeater {
                    model: root.categories
                    delegate: AbstractButton {
                        id: catBtn
                        required property var modelData
                        height: 32
                        implicitWidth: catRow.implicitWidth + 16
                        checkable: false
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus

                        readonly property bool selected: modelData.id === root.activeCategory

                        background: Rectangle {
                            radius: Theme.radiusSm
                            color: {
                                if (catBtn.selected)
                                    return Theme.panelSecondaryBg
                                if (catBtn.down)
                                    return Theme.panelMuted
                                if (catBtn.hovered)
                                    return Theme.popoverHover
                                return Theme.panelAccent
                            }
                            border.width: Theme.borderWidth
                            border.color: catBtn.selected
                                          ? Theme.panelSecondaryBorder
                                          : Theme.panelBorder
                        }

                        contentItem: Row {
                            id: catRow
                            anchors.centerIn: parent
                            spacing: 6

                            IconGlyph {
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: catBtn.modelData.icon
                                iconSize: 14
                                iconColor: catBtn.selected
                                           ? Theme.panelSecondaryForeground
                                           : Theme.panelForeground
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: catBtn.modelData.label
                                color: catBtn.selected
                                       ? Theme.panelSecondaryForeground
                                       : Theme.panelForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                font.weight: catBtn.selected ? Font.Medium : Font.Normal
                            }
                        }

                        onClicked: root.selectCategory(modelData.id)

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.NoButton
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }
            }
        }

        ThemedLabel {
            text: qsTr("Template")
            tone: "default"
            size: "sm"
        }

        Rectangle {
            width: parent.width
            height: Math.min(220, Math.max(44, root.categoryTemplates.length * 44) + 2)
            radius: Theme.radiusSm
            color: Theme.appBackground
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder
            clip: true

            ListView {
                id: templateList
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: root.categoryTemplates
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    width: templateList.width
                    height: 44
                    highlighted: modelData.id === root.templateId
                    hoverEnabled: true

                    background: Rectangle {
                        color: {
                            if (row.highlighted)
                                return Theme.panelSecondaryBg
                            if (row.hovered)
                                return Theme.popoverHover
                            return "transparent"
                        }
                    }

                    contentItem: Item {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10

                        Row {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10

                            IconGlyph {
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: row.modelData.icon
                                iconSize: 18
                                iconColor: row.highlighted
                                           ? Theme.panelSecondaryForeground
                                           : Theme.panelForeground
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 1

                                Text {
                                    text: row.modelData.label
                                    color: row.highlighted
                                           ? Theme.panelSecondaryForeground
                                           : Theme.panelForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSm
                                    font.weight: Font.Medium
                                }

                                Text {
                                    text: row.modelData.detail
                                    color: Theme.mutedForeground
                                    font.family: Theme.monoFontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }
                            }
                        }

                        // Mini aspect-ratio box on the right of each row.
                        Item {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 28
                            height: 28

                            readonly property real aw: {
                                switch (row.modelData.aspect) {
                                case "9:16": return 12
                                case "4:5": return 16
                                case "1:1": return 20
                                default: return 26
                                }
                            }
                            readonly property real ah: {
                                switch (row.modelData.aspect) {
                                case "9:16": return 22
                                case "4:5": return 20
                                case "1:1": return 20
                                default: return 15
                                }
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.aw
                                height: parent.ah
                                radius: 2
                                color: "transparent"
                                border.width: Theme.borderWidth
                                border.color: row.highlighted ? Theme.primary : Theme.panelBorder
                            }
                        }
                    }

                    onClicked: root.templateId = modelData.id
                }
            }
        }

        ThemedLabel {
            text: qsTr("Quality")
            tone: "default"
            size: "sm"
        }

        Flow {
            width: parent.width
            spacing: 6

            Repeater {
                model: root.qualities
                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    selected: root.qualityId === modelData.id
                    chipHeight: 28
                    onClicked: root.qualityId = modelData.id
                }
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingXl

            Rectangle {
                width: root.aspectBoxSize
                height: root.aspectBoxSize
                radius: Theme.radiusSm
                color: Theme.appBackground
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder

                Rectangle {
                    anchors.centerIn: parent
                    width: root.previewW
                    height: root.previewH
                    radius: 3
                    color: Theme.panelAccent
                    border.width: Theme.borderWidth
                    border.color: Theme.primary

                    Behavior on width {
                        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                    }
                    Behavior on height {
                        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: root.aspect
                        color: Theme.panelForeground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Medium
                    }
                }
            }

            Column {
                width: parent.width - root.aspectBoxSize - parent.spacing
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Row {
                    spacing: 8

                    IconGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: root.selectedTemplate.icon || ""
                        iconSize: 16
                        iconColor: Theme.panelForeground
                    }

                    ThemedLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        tone: "default"
                        size: "sm"
                        text: root.selectedTemplate.label
                    }
                }

                ThemedLabel {
                    width: parent.width
                    size: "sm"
                    font.family: Theme.monoFontFamily
                    text: qsTr("%1×%2 · %3")
                          .arg(root.outWidth)
                          .arg(root.outHeight)
                          .arg(root.aspect)
                }

                ThemedLabel {
                    width: parent.width
                    size: "xs"
                    text: qsTr("Preview shows the canvas aspect ratio")
                }
            }
        }
    }
}

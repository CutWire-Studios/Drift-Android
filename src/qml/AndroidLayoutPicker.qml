import QtQuick
import QtQuick.Controls.Basic
import Drift
import "components"

// Shared platform layout picker for the Android home screen (and settings).
// Category chips → template list → quality. Exposes outWidth/outHeight and apply().
Item {
    id: root

    property string activeCategory: "tiktok"
    property string templateId: "tiktok"
    property string qualityId: "1080p"
    // The project timebase. This picker is the only place it can be set on Android: the
    // desktop route (ProjectSetupDialog on the first clip) never opens here, because
    // applying a layout marks the project's canvas as decided.
    property int fps: 30
    // Only meaningful while the "Custom" template is selected.
    property int customWidth: 1080
    property int customHeight: 1920
    // When true, use a denser layout suited to a phone scroll page.
    property bool compact: true

    readonly property var categories: [
        { id: "youtube", label: qsTr("YouTube"), icon: Theme.icons.brandYoutube },
        { id: "instagram", label: qsTr("Instagram"), icon: Theme.icons.brandInstagram },
        { id: "facebook", label: qsTr("Facebook"), icon: Theme.icons.brandFacebook },
        { id: "tiktok", label: qsTr("TikTok"), icon: Theme.icons.brandTiktok },
        { id: "more", label: qsTr("More"), icon: Theme.icons.grid }
    ]

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
        { id: "portrait", category: "more", label: qsTr("Portrait"), detail: "9:16", aspect: "9:16", icon: Theme.icons.smartphone },
        { id: "classic", category: "more", label: qsTr("Classic"), detail: "4:3", aspect: "4:3", icon: Theme.icons.monitor },
        { id: "custom", category: "more", label: qsTr("Custom"), detail: qsTr("Any size"), aspect: "custom", icon: Theme.icons.sliders }
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

    readonly property string aspect: selectedTemplate.aspect || "9:16"
    readonly property bool customSize: aspect === "custom"
    // "custom" is a free size, not a ratio, so it is not something to print as one.
    readonly property string aspectLabel: customSize ? qsTr("Custom") : aspect

    readonly property int qualityEdge: {
        if (qualityId === "4k") return 2160
        if (qualityId === "720p") return 720
        return 1080
    }

    readonly property int outWidth: {
        if (customSize)
            return customWidth
        switch (aspect) {
        case "9:16": return qualityEdge
        case "4:5": return qualityEdge
        case "1:1": return qualityEdge
        case "4:3": return Math.round(qualityEdge * 4 / 3)
        case "16:9":
        default:
            if (qualityId === "4k") return 3840
            if (qualityId === "720p") return 1280
            return 1920
        }
    }

    readonly property int outHeight: {
        if (customSize)
            return customHeight
        switch (aspect) {
        case "9:16":
            if (qualityId === "4k") return 3840
            if (qualityId === "720p") return 1280
            return 1920
        case "4:5":
            return Math.round(outWidth * 5 / 4)
        case "1:1":
            return outWidth
        case "4:3":
            return qualityEdge
        case "16:9":
        default:
            if (qualityId === "4k") return 2160
            if (qualityId === "720p") return 720
            return 1080
        }
    }

    readonly property real aspectBoxSize: compact ? 64 : 88
    readonly property real aspectScale: {
        const w = Math.max(1, outWidth)
        const h = Math.max(1, outHeight)
        return Math.min(aspectBoxSize / w, aspectBoxSize / h)
    }
    readonly property real previewW: Math.max(8, outWidth * aspectScale)
    readonly property real previewH: Math.max(8, outHeight * aspectScale)

    implicitHeight: column.implicitHeight

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

    function resetForMobile() {
        activeCategory = "tiktok"
        templateId = "tiktok"
        qualityId = "1080p"
        fps = EditorState.projectFps()
        customWidth = EditorState.projectWidth()
        customHeight = EditorState.projectHeight()
    }

    function matchCurrentProject() {
        const w = EditorState.projectWidth()
        const h = EditorState.projectHeight()
        let bestId = "tiktok"
        let bestCat = "tiktok"
        let bestQuality = "1080p"
        let bestScore = Number.MAX_VALUE

        const qs = [
            { id: "4k", edge: 2160 },
            { id: "1080p", edge: 1080 },
            { id: "720p", edge: 720 }
        ]

        for (let i = 0; i < templates.length; ++i) {
            const t = templates[i]
            // A free size matches anything, so scoring it would have it win every time.
            if (t.aspect === "custom")
                continue
            for (let q = 0; q < qs.length; ++q) {
                const edge = qs[q].edge
                let tw = 0
                let th = 0
                switch (t.aspect) {
                case "9:16":
                    tw = edge
                    th = qs[q].id === "4k" ? 3840 : (qs[q].id === "720p" ? 1280 : 1920)
                    break
                case "4:5":
                    tw = edge
                    th = Math.round(edge * 5 / 4)
                    break
                case "1:1":
                    tw = edge
                    th = edge
                    break
                case "4:3":
                    tw = Math.round(edge * 4 / 3)
                    th = edge
                    break
                default:
                    tw = qs[q].id === "4k" ? 3840 : (qs[q].id === "720p" ? 1280 : 1920)
                    th = qs[q].id === "4k" ? 2160 : (qs[q].id === "720p" ? 720 : 1080)
                    break
                }
                const score = Math.abs(tw - w) + Math.abs(th - h)
                if (score < bestScore) {
                    bestScore = score
                    bestId = t.id
                    bestCat = t.category
                    bestQuality = qs[q].id
                }
            }
        }

        templateId = bestId
        activeCategory = bestCat
        qualityId = bestQuality
        fps = EditorState.projectFps()
        customWidth = w
        customHeight = h
    }

    function apply() {
        EditorState.setProjectSetup(outWidth, outHeight, fps)
        EditorState.markProjectLayoutChosen()
    }

    Column {
        id: column
        width: parent.width
        spacing: Theme.spacingLg

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("Choose a layout for your video")
        }

        Flickable {
            width: parent.width
            height: 36
            contentWidth: categoryRow.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick

            Row {
                id: categoryRow
                height: parent.height
                spacing: 6

                Repeater {
                    model: root.categories
                    delegate: AbstractButton {
                        id: catBtn
                        required property var modelData
                        height: 34
                        implicitWidth: catRow.implicitWidth + 16
                        hoverEnabled: true

                        readonly property bool selected: modelData.id === root.activeCategory

                        background: Rectangle {
                            radius: Theme.radiusSm
                            color: catBtn.selected ? Theme.panelSecondaryBg
                                 : (catBtn.down ? Theme.panelMuted : Theme.panelAccent)
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
            height: Math.min(root.compact ? 176 : 220,
                             Math.max(44, root.categoryTemplates.length * 44) + 2)
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
                    width: templateList.width
                    height: 44
                    highlighted: modelData.id === root.templateId

                    background: Rectangle {
                        color: row.highlighted ? Theme.panelSecondaryBg
                             : (row.hovered ? Theme.popoverHover : "transparent")
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

                        Item {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 28
                            height: 28
                            // Custom has no ratio to draw until the numbers are typed.
                            visible: row.modelData.aspect !== "custom"

                            readonly property real aw: {
                                switch (row.modelData.aspect) {
                                case "9:16": return 12
                                case "4:5": return 16
                                case "1:1": return 20
                                case "4:3": return 24
                                default: return 26
                                }
                            }
                            readonly property real ah: {
                                switch (row.modelData.aspect) {
                                case "9:16": return 22
                                case "4:5": return 20
                                case "1:1": return 20
                                case "4:3": return 18
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

        Row {
            width: parent.width
            spacing: Theme.spacingLg
            visible: root.customSize

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: Theme.spacingSm
                ThemedLabel { text: qsTr("Width") }
                ThemedNumberField {
                    width: parent.width
                    from: 16
                    to: 16384
                    step: 2
                    unit: "px"
                    value: root.customWidth
                    onEdited: v => root.customWidth = v
                }
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: Theme.spacingSm
                ThemedLabel { text: qsTr("Height") }
                ThemedNumberField {
                    width: parent.width
                    from: 16
                    to: 16384
                    step: 2
                    unit: "px"
                    value: root.customHeight
                    onEdited: v => root.customHeight = v
                }
            }
        }

        // The chip row read as an unlabelled row of numbers without this, and quality is
        // what the template does not decide — so hide it when the size is typed by hand.
        Column {
            width: parent.width
            spacing: Theme.spacingLg
            visible: !root.customSize

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
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel {
                text: qsTr("Frames per second")
                tone: "default"
                size: "sm"
            }

            ThemedNumberField {
                width: (parent.width - Theme.spacingLg) / 2
                from: 1
                to: 240
                unit: "fps"
                value: root.fps
                onEdited: v => root.fps = v
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

                    // A morph between two aspect ratios, not an entrance.
                    Behavior on width {
                        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easingInOut }
                    }
                    Behavior on height {
                        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easingInOut }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: root.aspectLabel
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
                    text: qsTr("%1×%2 · %3 · %4 fps")
                          .arg(root.outWidth)
                          .arg(root.outHeight)
                          .arg(root.aspectLabel)
                          .arg(root.fps)
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

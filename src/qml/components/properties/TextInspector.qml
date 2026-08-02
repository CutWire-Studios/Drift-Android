import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    readonly property bool hasTextStyle: hasSelection
                                         && (clipKind === "text" || clipKind === "subtitle")
                                         && !!clipData.textStyle
    readonly property var textStyle: hasTextStyle ? clipData.textStyle : ({
                                                                       "fontFamily": "Inter",
                                                                       "pixelSize": 64,
                                                                       "fontWeight": 700,
                                                                       "italic": false,
                                                                       "color": "#ffffffff",
                                                                       "align": "center",
                                                                       "valign": "middle",
                                                                       "wordWrap": true,
                                                                       "lineHeight": 1.2,
                                                                       "letterSpacing": 0,
                                                                       "outlineWidth": 0,
                                                                       "outlineColor": "#ff000000",
                                                                       "shadowEnabled": false,
                                                                       "shadowOffsetX": 0,
                                                                       "shadowOffsetY": 4,
                                                                       "shadowBlur": 8,
                                                                       "shadowOpacity": 0.6,
                                                                       "shadowColor": "#ff000000",
                                                                       "glowEnabled": false,
                                                                       "glowColor": "#ffffffff",
                                                                       "glowRadius": 18,
                                                                       "glowOpacity": 0.8,
                                                                       "boxEnabled": false,
                                                                       "boxColor": "#80000000",
                                                                       "boxPadding": 8,
                                                                       "boxRadius": 0,
                                                                       "packId": "",
                                                                       "wordHighlight": { "enabled": false, "color": "#ffe62828", "padding": 6, "radius": 4 },
                                                                       "underlineEnabled": false,
                                                                       "underlineColor": "#ffe62828",
                                                                       "underlineWidth": 6,
                                                                       "underlineOffset": 4,
                                                                       "accent": { "rule": "none", "n": 2, "phase": 0,
                                                                                   "colorEnabled": false, "color": "#ffffd640",
                                                                                   "sizeScale": 1, "outlineEnabled": false,
                                                                                   "outlineWidth": 0, "outlineColor": "#ff000000",
                                                                                   "highlight": { "enabled": false, "color": "#ffe62828", "padding": 6, "radius": 4 } },
                                                                       "animIn": { "kind": "none", "duration": 0.4, "ease": "easeOut" },
                                                                       "animOut": { "kind": "none", "duration": 0.4, "ease": "easeOut" }
                                                                   })

    // The selected family's real weight ladder — never an invented one. Single-weight display faces
    // (Anton, Bebas Neue, Pacifico...) expose exactly one entry and no italic.
    readonly property var fontFamilyInfo: {
        void clipDataRevision
        const catalog = EditorState.fontCatalog()
        for (let i = 0; i < catalog.length; ++i) {
            if (catalog[i].family === root.textStyle.fontFamily)
                return catalog[i]
        }
        return null
    }
    readonly property var availableWeights: fontFamilyInfo ? fontFamilyInfo.weights
                                                           : [100, 200, 300, 400, 500, 600, 700, 800, 900]
    readonly property bool familyHasItalic: fontFamilyInfo ? fontFamilyInfo.hasItalic : true

    readonly property var weightLabels: ({
                                             100: "Thin", 200: "ExtraLight", 300: "Light",
                                             400: "Regular", 500: "Medium", 600: "SemiBold",
                                             700: "Bold", 800: "ExtraBold", 900: "Black"
                                         })
    readonly property var animKinds: ["none", "fade", "slideUp", "slideDown", "slideLeft", "slideRight", "pop", "blur", "typewriter", "rise", "bounce", "wave"]
    readonly property var animKindLabels: ["None", "Fade", "Slide up", "Slide down", "Slide left", "Slide right", "Pop", "Blur", "Typewriter", "Rise", "Bounce", "Wave"]
    readonly property var easeKinds: ["linear", "easeOut", "easeInOut", "back"]
    readonly property var easeLabels: ["Linear", "Ease out", "Ease in-out", "Back"]
    readonly property var animUnits: ["block", "character", "word", "line"]
    readonly property var animUnitLabels: ["Whole block", "Character", "Word", "Line"]
    readonly property var animOrders: ["forward", "backward", "centerOut", "random"]
    readonly property var animOrderLabels: ["Forward", "Backward", "Center out", "Random"]
    readonly property var accentRules: ["none", "firstWord", "lastWord", "everyOther", "everyNth",
                                        "longestWord", "randomStable", "karaoke"]
    readonly property var accentRuleLabels: ["No accent", "First word", "Last word", "Every other word",
                                             "Every Nth word", "Longest word", "Random words",
                                             "Spoken word (karaoke)"]

    function setTextStyleKey(key, value) {
        const patch = {}
        patch[key] = value
        EditorState.setTextStyle(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }

    // Nested style groups: the C++ side takes the same partial-patch shape one level down.
    function setTextGroupKey(group, key, value) {
        const inner = {}
        inner[key] = value
        const patch = {}
        patch[group] = inner
        EditorState.setTextStyle(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }

    function setTextAccentHighlightKey(key, value) {
        const highlight = {}
        highlight[key] = value
        EditorState.setTextStyle(EditorState.selectedTrack, EditorState.selectedClip,
                                 { "accent": { "highlight": highlight } })
    }

    function setTextAnim(which, key, value) {
        const anim = {}
        anim[key] = value
        const patch = {}
        patch[which] = anim
        EditorState.setTextStyle(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }

    // Reveal granularity / stagger / order are shared by the entrance and exit, so write both.
    function setTextReveal(key, value) {
        const anim = {}
        anim[key] = value
        const patch = { "animIn": anim, "animOut": anim }
        EditorState.setTextStyle(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {
        if (textContentField && !textContentField.activeFocus)
            textContentField.text = root.clipData.textContent || ""
        if (!root.hasTextStyle)
            return
        const s = root.textStyle
        if (pixelSizeField && !pixelSizeField.activeFocus)
            pixelSizeField.value = s.pixelSize
        if (textColorField && !textColorField.activeFocus)
            textColorField.text = s.color
        if (lineHeightField && !lineHeightField.activeFocus)
            lineHeightField.value = s.lineHeight
        if (letterSpacingField && !letterSpacingField.activeFocus)
            letterSpacingField.value = s.letterSpacing
        if (outlineWidthField && !outlineWidthField.activeFocus)
            outlineWidthField.value = s.outlineWidth
        if (outlineColorField && !outlineColorField.activeFocus)
            outlineColorField.text = s.outlineColor
        if (shadowOffsetXField && !shadowOffsetXField.activeFocus)
            shadowOffsetXField.value = s.shadowOffsetX
        if (shadowOffsetYField && !shadowOffsetYField.activeFocus)
            shadowOffsetYField.value = s.shadowOffsetY
        if (shadowBlurField && !shadowBlurField.activeFocus)
            shadowBlurField.value = s.shadowBlur
        if (shadowOpacityField && !shadowOpacityField.activeFocus)
            shadowOpacityField.value = s.shadowOpacity
        if (shadowColorField && !shadowColorField.activeFocus)
            shadowColorField.text = s.shadowColor
        if (boxColorField && !boxColorField.activeFocus)
            boxColorField.text = s.boxColor
        if (boxPaddingField && !boxPaddingField.activeFocus)
            boxPaddingField.value = s.boxPadding
        if (boxRadiusField && !boxRadiusField.activeFocus)
            boxRadiusField.value = s.boxRadius
        if (glowRadiusField && !glowRadiusField.activeFocus)
            glowRadiusField.value = s.glowRadius
        if (glowOpacityField && !glowOpacityField.activeFocus)
            glowOpacityField.value = s.glowOpacity
        if (wordHighlightPaddingField && !wordHighlightPaddingField.activeFocus)
            wordHighlightPaddingField.value = s.wordHighlight.padding
        if (wordHighlightRadiusField && !wordHighlightRadiusField.activeFocus)
            wordHighlightRadiusField.value = s.wordHighlight.radius
        if (underlineWidthField && !underlineWidthField.activeFocus)
            underlineWidthField.value = s.underlineWidth
        if (underlineOffsetField && !underlineOffsetField.activeFocus)
            underlineOffsetField.value = s.underlineOffset
        if (accentEveryNField && !accentEveryNField.activeFocus)
            accentEveryNField.value = s.accent.n
        if (accentSizeScaleField && !accentSizeScaleField.activeFocus)
            accentSizeScaleField.value = s.accent.sizeScale
        if (accentOutlineWidthField && !accentOutlineWidthField.activeFocus)
            accentOutlineWidthField.value = s.accent.outlineWidth
        if (animInDurationField && !animInDurationField.activeFocus)
            animInDurationField.value = s.animIn.duration
        if (animOutDurationField && !animOutDurationField.activeFocus)
            animOutDurationField.value = s.animOut.duration
        if (animStaggerField && !animStaggerField.activeFocus)
            animStaggerField.value = s.animIn.stagger
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refreshFields() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refreshFields() }
        function onTracksChanged() { root.clipDataRevision++; root.refreshFields() }
    }

    Component.onCompleted: refreshFields()

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        Column {
            width: root.width
            spacing: 4
            visible: root.clipKind === "text"
            Text {
                text: qsTr("Text content")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            ThemedTextArea {
                id: textContentField
                width: parent.width
                height: 80
                onTextChanged: {
                    if (root.clipKind !== "text")
                        return
                    EditorState.previewSetClipTextContent(
                        EditorState.selectedTrack, EditorState.selectedClip, text)
                }
                onEditingFinished: EditorState.commitTextEdit(
                                       EditorState.selectedTrack, EditorState.selectedClip, text)
            }
        }

        Column {
            width: root.width
            spacing: 8
            visible: root.hasTextStyle

            Text {
                text: qsTr("Style packs")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            // Cards render through the real rasterizer (image://textstyle), so what a
            // pack looks like here is what the compositor draws — accents included.
            Grid {
                id: packGrid
                width: parent.width
                columns: 2
                spacing: 8
                readonly property real cellWidth: (width - spacing * (columns - 1)) / columns

                Repeater {
                    model: EditorState.textPresets()
                    delegate: Column {
                        id: packCard
                        required property var modelData
                        readonly property bool selected: root.textStyle.packId === modelData.id
                        width: packGrid.cellWidth
                        spacing: 3

                        Text {
                            width: parent.width
                            text: packCard.modelData.label
                            elide: Text.ElideRight
                            color: packCard.selected ? Theme.primary : Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }

                        Rectangle {
                            width: parent.width
                            height: Math.round(width * 0.45)
                            radius: Theme.radiusSm
                            color: Theme.panelSecondaryBg
                            border.width: packCard.selected ? Theme.borderWidthFocus : Theme.borderWidth
                            border.color: packCard.selected ? Theme.primary : Theme.panelSecondaryBorder
                            clip: true

                            Image {
                                anchors.fill: parent
                                anchors.margins: 2
                                asynchronous: true
                                fillMode: Image.Pad
                                source: "image://textstyle/" + packCard.modelData.id
                                sourceSize.width: Math.max(1, Math.round(width))
                                sourceSize.height: Math.max(1, Math.round(height))
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: EditorState.applyTextPreset(
                                               EditorState.selectedTrack,
                                               EditorState.selectedClip,
                                               packCard.modelData.id)
                            }
                        }
                    }
                }
            }

            Text {
                text: qsTr("Font")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            FontPicker {
                width: parent.width
                family: root.textStyle.fontFamily
                onFamilyPicked: family => root.setTextStyleKey("fontFamily", family)
            }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Weight")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedComboBox {
                        id: fontWeightBox
                        width: parent.width
                        // Only the weights this family actually ships.
                        model: root.availableWeights.map(
                                   w => root.weightLabels[w] || String(w))
                        currentIndex: Math.max(0, root.availableWeights.indexOf(root.textStyle.fontWeight))
                        onActivated: root.setTextStyleKey("fontWeight", root.availableWeights[currentIndex])
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Size")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: pixelSizeField
                        unit: "px"
                        width: parent.width
                        decimals: 0
                        step: 1
                        from: 1
                        to: 500
                        onEdited: v => root.setTextStyleKey("pixelSize", v)
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Color")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    Row {
                        spacing: 6
                        Rectangle {
                            width: Theme.spacing3xl
                            height: Theme.spacing3xl
                            radius: Theme.radiusSm
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.textStyle.color
                            border.width: swatch_color.containsMouse ? Theme.borderWidthFocus : Theme.borderWidth
                            border.color: swatch_color.containsMouse ? Theme.primary : Theme.panelBorder

                            Behavior on border.color {
                                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }

                            ThemedToolTip {
                                text: qsTr("Choose text colour")
                                visible: swatch_color.containsMouse
                            }

                            MouseArea {
                                id: swatch_color
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    styleColorDialog.targetStyleKey = "color"
                                    styleColorDialog.selectedColor = root.textStyle.color
                                    styleColorDialog.open()
                                }
                            }
                        }
                        ThemedTextField {
                            id: textColorField
                            width: 92
                            text: root.textStyle.color
                            color: Theme.panelForeground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeSm
                            // Rejects malformed input instead of silently applying a typo.
                            readonly property bool validHex:
                                /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text)
                            errorText: validHex || text.length === 0 ? "" : qsTr("Enter a color like #FF0000")
                            onEditingFinished: if (validHex) root.setTextStyleKey("color", text)
                        }
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Style")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedToggleButton {
                        width: 60
                        text: qsTr("Italic")
                        checked: root.textStyle.italic
                        // Single-face display fonts have no italic.
                        enabled: root.familyHasItalic
                        tooltip: root.familyHasItalic
                                 ? qsTr("Italicise the text")
                                 : qsTr("%1 has no italic face").arg(root.textStyle.fontFamily)
                        onClicked: root.setTextStyleKey("italic", !root.textStyle.italic)
                    }
                }
            }

            // Alignment groups. These were two visually identical
            // rows of L/C/R and T/M/B boxes, distinguishable only
            // by their tooltips; they now carry real icons and are
            // keyboard-operable.
            Row {
                width: parent.width
                spacing: Theme.spacingMd

                Repeater {
                    model: [
                        { value: "left",   glyph: Theme.icons.alignLeft,   label: qsTr("Align left") },
                        { value: "center", glyph: Theme.icons.alignCenter, label: qsTr("Align centre") },
                        { value: "right",  glyph: Theme.icons.alignRight,  label: qsTr("Align right") }
                    ]
                    delegate: ThemedToggleButton {
                        required property var modelData
                        width: 34
                        glyph: modelData.glyph
                        checked: root.textStyle.align === modelData.value
                        tooltip: modelData.label
                        onClicked: root.setTextStyleKey("align", modelData.value)
                    }
                }

                Rectangle {
                    width: Theme.borderWidth
                    height: Theme.controlHeightSm
                    color: Theme.panelBorder
                }

                Repeater {
                    model: [
                        { value: "top",    glyph: Theme.icons.alignTop,    label: qsTr("Align top") },
                        { value: "middle", glyph: Theme.icons.alignMiddle, label: qsTr("Align middle") },
                        { value: "bottom", glyph: Theme.icons.alignBottom, label: qsTr("Align bottom") }
                    ]
                    delegate: ThemedToggleButton {
                        required property var modelData
                        width: 34
                        glyph: modelData.glyph
                        checked: root.textStyle.valign === modelData.value
                        tooltip: modelData.label
                        onClicked: root.setTextStyleKey("valign", modelData.value)
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Line height")
                        HoverHandler { id: tipHover761 }
                        ThemedToolTip { text: qsTr("Vertical spacing between lines, as a multiple of the font size"); visible: tipHover761.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: lineHeightField
                        width: parent.width
                        decimals: 2
                        step: 0.05
                        from: 0.5
                        to: 4
                        onEdited: v => root.setTextStyleKey("lineHeight", v)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Letter spacing")
                        HoverHandler { id: tipHover781 }
                        ThemedToolTip { text: qsTr("Extra space between characters, in pixels"); visible: tipHover781.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: letterSpacingField
                        to: 200
                        from: -100
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 0.5
                        onEdited: v => root.setTextStyleKey("letterSpacing", v)
                    }
                }
            }

            ThemedToggleButton {
                width: 96
                text: qsTr("Word wrap")
                checked: root.textStyle.wordWrap
                tooltip: qsTr("Wrap long lines inside the text box instead of overflowing")
                onClicked: root.setTextStyleKey("wordWrap", !root.textStyle.wordWrap)
            }

            Text {
                text: qsTr("Outline")
                HoverHandler { id: tipHover808 }
                ThemedToolTip { text: qsTr("Outline around each letter"); visible: tipHover808.hovered }
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Width")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: outlineWidthField
                        to: 100
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 0.5
                        from: 0
                        onEdited: v => root.setTextStyleKey("outlineWidth", v)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Color")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    Row {
                        spacing: 6
                        Rectangle {
                            width: Theme.spacing3xl
                            height: Theme.spacing3xl
                            radius: Theme.radiusSm
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.textStyle.outlineColor
                            border.width: swatch_outlineColor.containsMouse ? Theme.borderWidthFocus : Theme.borderWidth
                            border.color: swatch_outlineColor.containsMouse ? Theme.primary : Theme.panelBorder

                            Behavior on border.color {
                                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }

                            ThemedToolTip {
                                text: qsTr("Choose outline colour")
                                visible: swatch_outlineColor.containsMouse
                            }

                            MouseArea {
                                id: swatch_outlineColor
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    styleColorDialog.targetStyleKey = "outlineColor"
                                    styleColorDialog.selectedColor = root.textStyle.outlineColor
                                    styleColorDialog.open()
                                }
                            }
                        }
                        ThemedTextField {
                            id: outlineColorField
                            width: 92
                            text: root.textStyle.outlineColor
                            color: Theme.panelForeground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeSm
                            // Rejects malformed input instead of silently applying a typo.
                            readonly property bool validHex:
                                /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text)
                            errorText: validHex || text.length === 0 ? "" : qsTr("Enter a color like #FF0000")
                            onEditingFinished: if (validHex) root.setTextStyleKey("outlineColor", text)
                        }
                    }
                }
            }

            ThemedToggleButton {
                width: 96
                text: qsTr("Shadow")
                checked: root.textStyle.shadowEnabled
                tooltip: qsTr("Draw a drop shadow behind the text")
                onClicked: root.setTextStyleKey("shadowEnabled", !root.textStyle.shadowEnabled)
            }

            Row {
                width: parent.width
                spacing: 8
                visible: root.textStyle.shadowEnabled

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Offset X")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: shadowOffsetXField
                        to: 500
                        from: -500
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 1
                        onEdited: v => root.setTextStyleKey("shadowOffsetX", v)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Offset Y")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: shadowOffsetYField
                        to: 500
                        from: -500
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 1
                        onEdited: v => root.setTextStyleKey("shadowOffsetY", v)
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8
                visible: root.textStyle.shadowEnabled

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Blur")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: shadowBlurField
                        to: 100
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 1
                        from: 0
                        onEdited: v => root.setTextStyleKey("shadowBlur", v)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Opacity")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: shadowOpacityField
                        width: parent.width
                        decimals: 2
                        step: 0.05
                        from: 0
                        to: 1
                        onEdited: v => root.setTextStyleKey("shadowOpacity", v)
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 6
                visible: root.textStyle.shadowEnabled

                Text {
                    text: qsTr("Shadow color")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    width: Theme.spacing3xl
                    height: Theme.spacing3xl
                    radius: Theme.radiusSm
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.textStyle.shadowColor
                    border.width: swatch_shadowColor.containsMouse ? Theme.borderWidthFocus : Theme.borderWidth
                    border.color: swatch_shadowColor.containsMouse ? Theme.primary : Theme.panelBorder

                    Behavior on border.color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    ThemedToolTip {
                        text: qsTr("Choose shadow colour")
                        visible: swatch_shadowColor.containsMouse
                    }

                    MouseArea {
                        id: swatch_shadowColor
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            styleColorDialog.targetStyleKey = "shadowColor"
                            styleColorDialog.selectedColor = root.textStyle.shadowColor
                            styleColorDialog.open()
                        }
                    }
                }
                ThemedTextField {
                    id: shadowColorField
                    width: 92
                    text: root.textStyle.shadowColor
                    color: Theme.panelForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeSm
                    // Rejects malformed input instead of silently applying a typo.
                    readonly property bool validHex:
                        /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text)
                    errorText: validHex || text.length === 0 ? "" : qsTr("Enter a color like #FF0000")
                    onEditingFinished: if (validHex) root.setTextStyleKey("shadowColor", text)
                }
            }

            ThemedToggleButton {
                width: 96
                text: qsTr("Background")
                checked: root.textStyle.boxEnabled
                tooltip: qsTr("Draw a filled box behind the text")
                onClicked: root.setTextStyleKey("boxEnabled", !root.textStyle.boxEnabled)
            }

            Row {
                width: parent.width
                spacing: 6
                visible: root.textStyle.boxEnabled

                Rectangle {
                    width: Theme.spacing3xl
                    height: Theme.spacing3xl
                    radius: Theme.radiusSm
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.textStyle.boxColor
                    border.width: swatch_boxColor.containsMouse ? Theme.borderWidthFocus : Theme.borderWidth
                    border.color: swatch_boxColor.containsMouse ? Theme.primary : Theme.panelBorder

                    Behavior on border.color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    ThemedToolTip {
                        text: qsTr("Choose background colour")
                        visible: swatch_boxColor.containsMouse
                    }

                    MouseArea {
                        id: swatch_boxColor
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            styleColorDialog.targetStyleKey = "boxColor"
                            styleColorDialog.selectedColor = root.textStyle.boxColor
                            styleColorDialog.open()
                        }
                    }
                }
                ThemedTextField {
                    id: boxColorField
                    width: 92
                    text: root.textStyle.boxColor
                    color: Theme.panelForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeSm
                    // Rejects malformed input instead of silently applying a typo.
                    readonly property bool validHex:
                        /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text)
                    errorText: validHex || text.length === 0 ? "" : qsTr("Enter a color like #FF0000")
                    onEditingFinished: if (validHex) root.setTextStyleKey("boxColor", text)
                }
            }

            Row {
                width: parent.width
                spacing: 8
                visible: root.textStyle.boxEnabled

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Padding")
                        HoverHandler { id: tipHover1088 }
                        ThemedToolTip { text: qsTr("Space between the text and the edge of its background box"); visible: tipHover1088.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: boxPaddingField
                        to: 500
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 1
                        from: 0
                        onEdited: v => root.setTextStyleKey("boxPadding", v)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Corner radius")
                        HoverHandler { id: tipHover1109 }
                        ThemedToolTip { text: qsTr("Roundness of the background box corners"); visible: tipHover1109.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: boxRadiusField
                        to: 500
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 1
                        from: 0
                        onEdited: v => root.setTextStyleKey("boxRadius", v)
                    }
                }
            }

            ThemedToggleButton {
                width: 96
                text: qsTr("Glow")
                checked: root.textStyle.glowEnabled
                tooltip: qsTr("Coloured bloom around the letters, with no offset")
                onClicked: root.setTextStyleKey("glowEnabled", !root.textStyle.glowEnabled)
            }

            Row {
                width: parent.width
                spacing: 8
                visible: root.textStyle.glowEnabled

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Radius")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: glowRadiusField
                        to: 200
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 1
                        from: 0
                        onEdited: v => root.setTextStyleKey("glowRadius", v)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Opacity")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: glowOpacityField
                        to: 1
                        width: parent.width
                        decimals: 2
                        step: 0.05
                        from: 0
                        onEdited: v => root.setTextStyleKey("glowOpacity", v)
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                visible: root.textStyle.glowEnabled
                Text {
                    text: qsTr("Glow colour")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ColorSwatchField {
                    hex: root.textStyle.glowColor
                    tooltip: qsTr("Choose glow colour")
                    onEdited: value => root.setTextStyleKey("glowColor", value)
                }
            }

            ThemedToggleButton {
                width: 128
                text: qsTr("Word highlight")
                checked: root.textStyle.wordHighlight.enabled
                tooltip: qsTr("Filled pill behind every word, sized to the word itself")
                onClicked: root.setTextGroupKey("wordHighlight", "enabled",
                                                !root.textStyle.wordHighlight.enabled)
            }

            Row {
                width: parent.width
                spacing: 8
                visible: root.textStyle.wordHighlight.enabled

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Thickness")
                        HoverHandler { id: tipHoverHlPad }
                        ThemedToolTip { text: qsTr("How far the pill extends past the word"); visible: tipHoverHlPad.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: wordHighlightPaddingField
                        to: 200
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 1
                        from: 0
                        onEdited: v => root.setTextGroupKey("wordHighlight", "padding", v)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Corner radius")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: wordHighlightRadiusField
                        to: 200
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 1
                        from: 0
                        onEdited: v => root.setTextGroupKey("wordHighlight", "radius", v)
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                visible: root.textStyle.wordHighlight.enabled
                Text {
                    text: qsTr("Highlight colour")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ColorSwatchField {
                    hex: root.textStyle.wordHighlight.color
                    tooltip: qsTr("Choose highlight colour")
                    onEdited: value => root.setTextGroupKey("wordHighlight", "color", value)
                }
            }

            ThemedToggleButton {
                width: 96
                text: qsTr("Underline")
                checked: root.textStyle.underlineEnabled
                tooltip: qsTr("Draw a rule under each line of text")
                onClicked: root.setTextStyleKey("underlineEnabled", !root.textStyle.underlineEnabled)
            }

            Row {
                width: parent.width
                spacing: 8
                visible: root.textStyle.underlineEnabled

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Thickness")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: underlineWidthField
                        to: 100
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 0.5
                        from: 0
                        onEdited: v => root.setTextStyleKey("underlineWidth", v)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Offset")
                        HoverHandler { id: tipHoverUnderlineOffset }
                        ThemedToolTip { text: qsTr("Gap between the baseline and the rule"); visible: tipHoverUnderlineOffset.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: underlineOffsetField
                        to: 200
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 1
                        from: -200
                        onEdited: v => root.setTextStyleKey("underlineOffset", v)
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                visible: root.textStyle.underlineEnabled
                Text {
                    text: qsTr("Underline colour")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ColorSwatchField {
                    hex: root.textStyle.underlineColor
                    tooltip: qsTr("Choose underline colour")
                    onEdited: value => root.setTextStyleKey("underlineColor", value)
                }
            }

            Text {
                text: qsTr("Word accent")
                HoverHandler { id: tipHoverAccent }
                ThemedToolTip {
                    text: qsTr("Style some words differently from the rest, chosen by rule")
                    visible: tipHoverAccent.hovered
                }
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Row {
                width: parent.width
                spacing: 8

                ThemedComboBox {
                    id: accentRuleBox
                    width: root.textStyle.accent.rule === "everyNth"
                           ? (parent.width - parent.spacing) * 0.66 : parent.width
                    model: root.accentRuleLabels
                    currentIndex: Math.max(0, root.accentRules.indexOf(root.textStyle.accent.rule))
                    onActivated: root.setTextGroupKey("accent", "rule",
                                                      root.accentRules[currentIndex])
                }

                ThemedNumberField {
                    id: accentEveryNField
                    visible: root.textStyle.accent.rule === "everyNth"
                    width: (parent.width - parent.spacing) * 0.34
                    to: 16
                    from: 1
                    decimals: 0
                    step: 1
                    onEdited: v => root.setTextGroupKey("accent", "n", v)
                }
            }

            Column {
                width: parent.width
                spacing: 8
                visible: root.textStyle.accent.rule !== "none"

                Row {
                    width: parent.width
                    spacing: 8

                    ThemedToggleButton {
                        width: 112
                        text: qsTr("Accent colour")
                        checked: root.textStyle.accent.colorEnabled
                        tooltip: qsTr("Recolour the words the rule picks out")
                        onClicked: root.setTextGroupKey("accent", "colorEnabled",
                                                        !root.textStyle.accent.colorEnabled)
                    }

                    ColorSwatchField {
                        visible: root.textStyle.accent.colorEnabled
                        anchors.verticalCenter: parent.verticalCenter
                        hex: root.textStyle.accent.color
                        tooltip: qsTr("Choose accent colour")
                        onEdited: value => root.setTextGroupKey("accent", "color", value)
                    }
                }

                Column {
                    width: (parent.width - 8) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Accent size")
                        HoverHandler { id: tipHoverAccentSize }
                        ThemedToolTip {
                            text: qsTr("Size of the accented words relative to the rest of the line")
                            visible: tipHoverAccentSize.hovered
                        }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: accentSizeScaleField
                        to: 4
                        unit: "x"
                        width: parent.width
                        decimals: 2
                        step: 0.05
                        from: 0.25
                        onEdited: v => root.setTextGroupKey("accent", "sizeScale", v)
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8

                    ThemedToggleButton {
                        width: 112
                        text: qsTr("Accent outline")
                        checked: root.textStyle.accent.outlineEnabled
                        tooltip: qsTr("Give the accented words their own outline")
                        onClicked: root.setTextGroupKey("accent", "outlineEnabled",
                                                        !root.textStyle.accent.outlineEnabled)
                    }

                    ColorSwatchField {
                        visible: root.textStyle.accent.outlineEnabled
                        anchors.verticalCenter: parent.verticalCenter
                        hex: root.textStyle.accent.outlineColor
                        tooltip: qsTr("Choose accent outline colour")
                        onEdited: value => root.setTextGroupKey("accent", "outlineColor", value)
                    }
                }

                Column {
                    width: (parent.width - 8) / 2
                    spacing: 4
                    visible: root.textStyle.accent.outlineEnabled
                    Text {
                        text: qsTr("Accent outline width")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: accentOutlineWidthField
                        to: 100
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 0.5
                        from: 0
                        onEdited: v => root.setTextGroupKey("accent", "outlineWidth", v)
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8

                    ThemedToggleButton {
                        width: 112
                        text: qsTr("Accent pill")
                        checked: root.textStyle.accent.highlight.enabled
                        tooltip: qsTr("Highlight only the accented words, instead of every word")
                        onClicked: root.setTextAccentHighlightKey(
                                       "enabled", !root.textStyle.accent.highlight.enabled)
                    }

                    ColorSwatchField {
                        visible: root.textStyle.accent.highlight.enabled
                        anchors.verticalCenter: parent.verticalCenter
                        hex: root.textStyle.accent.highlight.color
                        tooltip: qsTr("Choose accent highlight colour")
                        onEdited: value => root.setTextAccentHighlightKey("color", value)
                    }
                }
            }

            Text {
                text: qsTr("Animation")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Row {
                width: parent.width
                spacing: 6

                Text {
                    text: qsTr("In")
                    width: 20
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    anchors.verticalCenter: parent.verticalCenter
                }
                ThemedComboBox {
                    id: animInKindBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.animKindLabels
                    currentIndex: Math.max(0, root.animKinds.indexOf(root.textStyle.animIn.kind))
                    onActivated: root.setTextAnim("animIn", "kind", root.animKinds[currentIndex])
                }
                ThemedComboBox {
                    id: animInEaseBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.easeLabels
                    currentIndex: Math.max(0, root.easeKinds.indexOf(root.textStyle.animIn.ease))
                    onActivated: root.setTextAnim("animIn", "ease", root.easeKinds[currentIndex])
                }
                ThemedNumberField {
                    id: animInDurationField
                    to: 60
                    unit: "s"
                    width: 56
                    decimals: 2
                    step: 0.05
                    from: 0
                    onEdited: v => root.setTextAnim("animIn", "duration", v)
                }
            }

            Row {
                width: parent.width
                spacing: 6

                Text {
                    text: qsTr("Out")
                    width: 20
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    anchors.verticalCenter: parent.verticalCenter
                }
                ThemedComboBox {
                    id: animOutKindBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.animKindLabels
                    currentIndex: Math.max(0, root.animKinds.indexOf(root.textStyle.animOut.kind))
                    onActivated: root.setTextAnim("animOut", "kind", root.animKinds[currentIndex])
                }
                ThemedComboBox {
                    id: animOutEaseBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.easeLabels
                    currentIndex: Math.max(0, root.easeKinds.indexOf(root.textStyle.animOut.ease))
                    onActivated: root.setTextAnim("animOut", "ease", root.easeKinds[currentIndex])
                }
                ThemedNumberField {
                    id: animOutDurationField
                    to: 60
                    unit: "s"
                    width: 56
                    decimals: 2
                    step: 0.05
                    from: 0
                    onEdited: v => root.setTextAnim("animOut", "duration", v)
                }
            }

            // Reveal granularity: stagger the entrance/exit across characters,
            // words or lines instead of moving the whole block at once.
            Row {
                width: parent.width
                spacing: 6

                Text {
                    text: qsTr("By")
                    width: 20
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    anchors.verticalCenter: parent.verticalCenter
                }
                ThemedComboBox {
                    id: animUnitBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.animUnitLabels
                    currentIndex: Math.max(0, root.animUnits.indexOf(root.textStyle.animIn.unit))
                    onActivated: root.setTextReveal("unit", root.animUnits[currentIndex])
                }
                ThemedComboBox {
                    id: animOrderBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.animOrderLabels
                    enabled: root.textStyle.animIn.unit !== "block"
                    currentIndex: Math.max(0, root.animOrders.indexOf(root.textStyle.animIn.order))
                    onActivated: root.setTextReveal("order", root.animOrders[currentIndex])
                }
                ThemedNumberField {
                    id: animStaggerField
                    to: 2
                    unit: "s"
                    width: 56
                    decimals: 2
                    step: 0.02
                    from: 0
                    enabled: root.textStyle.animIn.unit !== "block"
                    onEdited: v => root.setTextReveal("stagger", v)
                }
            }
        }
    }

    ColorDialog {
        id: styleColorDialog
        title: qsTr("Select Color")
        property string targetStyleKey: ""

        function colorToHex(c) {
            var toHex = function(v) {
                var h = Math.round(v * 255).toString(16);
                return h.length === 1 ? "0" + h : h;
            }
            return "#" + toHex(c.a) + toHex(c.r) + toHex(c.g) + toHex(c.b);
        }

        onAccepted: {
            if (targetStyleKey !== "") {
                root.setTextStyleKey(targetStyleKey, colorToHex(selectedColor))
            }
        }
    }
}

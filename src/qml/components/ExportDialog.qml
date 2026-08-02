import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

ThemedDialog {
    id: root

    title: qsTr("Export video")
    acceptText: qsTr("Export")
    preferredWidth: Theme.dialogWidthLg

    property string scaleId: "source"
    property int targetHeight: 0
    property int videoBitrateKbps: 12000
    property string videoCodecId: "h264"
    property string rateControl: "crf"
    property int crf: 23
    property string videoPreset: "medium"
    property string audioCodecId: "aac"
    property int audioBitrateKbps: 192
    property bool advancedOpen: false

    property var videoCodecs: []
    property var audioCodecs: []
    property var scaleOptions: []
    property var currentVideoCodec: ({})
    property var currentAudioCodec: ({})

    readonly property bool videoLossless: !!(currentVideoCodec && currentVideoCodec.lossless)
    readonly property bool videoSupportsCrf: !!(currentVideoCodec && currentVideoCodec.supportsCrf)
    readonly property bool videoSupportsBitrate: !!(currentVideoCodec && currentVideoCodec.supportsBitrate)
    readonly property bool videoSupportsPreset: !!(currentVideoCodec && currentVideoCodec.supportsPreset)
    readonly property var videoPresetList: (currentVideoCodec && currentVideoCodec.presets) ? currentVideoCodec.presets : []
    readonly property bool audioLossless: !!(currentAudioCodec && currentAudioCodec.lossless)

    function codecById(list, id) {
        for (var i = 0; i < list.length; ++i) {
            if (list[i].id === id)
                return list[i]
        }
        return null
    }

    function firstAvailableId(list, preferred) {
        var pref = codecById(list, preferred)
        if (pref && pref.available)
            return preferred
        for (var i = 0; i < list.length; ++i) {
            if (list[i].available)
                return list[i].id
        }
        return preferred
    }

    function refreshCodecMeta() {
        currentVideoCodec = codecById(videoCodecs, videoCodecId) || {}
        currentAudioCodec = codecById(audioCodecs, audioCodecId) || {}
        if (videoLossless)
            rateControl = "crf"
        else if (!videoSupportsCrf && videoSupportsBitrate)
            rateControl = "bitrate"
        else if (videoSupportsCrf && rateControl !== "bitrate")
            rateControl = "crf"
    }

    function applyVideoCodecDefaults(codec) {
        if (!codec)
            return
        if (codec.defaultCrf !== undefined)
            crf = codec.defaultCrf
        if (codec.defaultPreset)
            videoPreset = codec.defaultPreset
        if (codec.lossless)
            rateControl = "crf"
        else if (!codec.supportsCrf && codec.supportsBitrate)
            rateControl = "bitrate"
        else if (codec.supportsCrf)
            rateControl = "crf"
    }

    function openDialog() {
        videoCodecs = EditorState.exportVideoCodecs()
        audioCodecs = EditorState.exportAudioCodecs()
        scaleOptions = EditorState.exportScaleOptions()

        var defaults = EditorState.exportDefaultSettings()
        videoCodecId = firstAvailableId(videoCodecs, defaults.videoCodecId || "h264")
        audioCodecId = firstAvailableId(audioCodecs, defaults.audioCodecId || "aac")
        rateControl = defaults.rateControl || "crf"
        crf = defaults.crf || 23
        videoBitrateKbps = defaults.videoBitrateKbps || 12000
        videoPreset = defaults.videoPreset || "medium"
        audioBitrateKbps = defaults.audioBitrateKbps || 192
        advancedOpen = false

        if (scaleOptions.length > 0) {
            scaleId = scaleOptions[0].id
            targetHeight = scaleOptions[0].targetHeight
            if (scaleOptions[0].videoBitrateKbps)
                videoBitrateKbps = scaleOptions[0].videoBitrateKbps
        } else {
            scaleId = "source"
            targetHeight = 0
        }

        applyVideoCodecDefaults(codecById(videoCodecs, videoCodecId))
        refreshCodecMeta()
        syncComboIndices()
        open()
    }

    function syncComboIndices() {
        videoCodecCombo.currentIndex = Math.max(0, videoCodecCombo.indexOfValue(videoCodecId))
        audioCodecCombo.currentIndex = Math.max(0, audioCodecCombo.indexOfValue(audioCodecId))
        if (videoSupportsPreset && videoPresetList.length > 0) {
            var pi = videoPresetList.indexOf(videoPreset)
            presetCombo.currentIndex = pi >= 0 ? pi : Math.max(0, videoPresetList.indexOf(
                                                                   currentVideoCodec.defaultPreset || ""))
            if (presetCombo.currentIndex >= 0 && presetCombo.currentIndex < videoPresetList.length)
                videoPreset = videoPresetList[presetCombo.currentIndex]
        }
    }

    function buildSettings() {
        return {
            "targetHeight": targetHeight,
            "videoCodecId": videoCodecId,
            "rateControl": videoLossless ? "crf" : rateControl,
            "crf": crf,
            "videoBitrateKbps": videoBitrateKbps,
            "videoPreset": videoPreset,
            "audioCodecId": audioCodecId,
            "audioBitrateKbps": audioBitrateKbps
        }
    }

    onAccepted: {
        var container = EditorState.exportPreferredContainer(videoCodecId, audioCodecId)
        var filters = EditorState.exportSaveFilters(container)
        var suffix = EditorState.exportDefaultSuffix(container)
        var url = FileDialogs.saveFile(qsTr("Export Video"), filters,
                                       EditorState.projectName, suffix)
        if (url != "") {
            EditorState.exportWithSettings(url, buildSettings())
            Toasts.info(qsTr("Export started…"))
        } else {
            Toasts.info(qsTr("Export cancelled."))
        }
    }

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : Theme.dialogWidthLg

        ThemedLabel {
            width: parent.width
            size: "sm"
            text: qsTr("Saves what you see in the preview. Pick a size — the picture shape stays the same.")
        }

        ThemedLabel {
            text: qsTr("Downscale")
        }

        Flow {
            width: parent.width
            spacing: Theme.spacingMd

            Repeater {
                model: root.scaleOptions

                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    selected: root.scaleId === modelData.id
                    tooltip: qsTr("Export at %1×%2").arg(modelData.width).arg(modelData.height)
                    onClicked: {
                        root.scaleId = modelData.id
                        root.targetHeight = modelData.targetHeight
                        if (modelData.videoBitrateKbps)
                            root.videoBitrateKbps = modelData.videoBitrateKbps
                    }
                }
            }
        }

        // Advanced disclosure
        Item {
            width: parent.width
            height: advancedHeader.implicitHeight

            Row {
                id: advancedHeader
                spacing: Theme.spacingSm
                anchors.left: parent.left

                IconGlyph {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.chevronRight
                    iconSize: Theme.iconSizeSm
                    iconColor: Theme.mutedForeground
                    rotation: root.advancedOpen ? 90 : 0
                    Behavior on rotation {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                }

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Advanced")
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.advancedOpen = !root.advancedOpen
            }

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Advanced")
            Accessible.checkable: true
            Accessible.checked: root.advancedOpen
            Accessible.onPressAction: root.advancedOpen = !root.advancedOpen
        }

        Column {
            width: parent.width
            spacing: Theme.spacingXl
            visible: root.advancedOpen
            height: visible ? implicitHeight : 0
            clip: true

            // Video encoder
            Column {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedLabel {
                    text: qsTr("Video encoder")
                    size: "sm"
                }

                ThemedComboBox {
                    id: videoCodecCombo
                    width: parent.width
                    textRole: "label"
                    valueRole: "id"
                    model: root.videoCodecs
                    onActivated: function (index) {
                        var item = root.videoCodecs[index]
                        if (!item || !item.available) {
                            currentIndex = Math.max(0, indexOfValue(root.videoCodecId))
                            return
                        }
                        root.videoCodecId = item.id
                        root.applyVideoCodecDefaults(item)
                        root.refreshCodecMeta()
                        root.syncComboIndices()
                    }
                }
            }

            // Rate control (hidden for lossless)
            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: !root.videoLossless

                Row {
                    spacing: Theme.spacingMd

                    ThemedToggleButton {
                        text: qsTr("Constant Quality")
                        checked: root.rateControl === "crf"
                        enabled: root.videoSupportsCrf
                        onClicked: {
                            if (root.videoSupportsCrf)
                                root.rateControl = "crf"
                        }
                    }

                    ThemedToggleButton {
                        text: qsTr("Bitrate")
                        checked: root.rateControl === "bitrate"
                        enabled: root.videoSupportsBitrate
                        onClicked: {
                            if (root.videoSupportsBitrate)
                                root.rateControl = "bitrate"
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm
                    visible: root.rateControl === "crf" && root.videoSupportsCrf

                    Row {
                        width: parent.width
                        ThemedLabel {
                            text: qsTr("RF %1").arg(root.crf)
                            size: "sm"
                        }
                    }

                    ThemedSlider {
                        width: parent.width
                        from: 0
                        to: 51
                        stepSize: 1
                        value: root.crf
                        valueFormatter: function (v) { return qsTr("RF %1").arg(Math.round(v)) }
                        onMoved: root.crf = Math.round(value)
                    }

                    RowLayout {
                        width: parent.width

                        ThemedLabel {
                            text: qsTr("Higher quality")
                            size: "xs"
                        }

                        Item { Layout.fillWidth: true }

                        ThemedLabel {
                            text: qsTr("Lower quality")
                            size: "xs"
                        }
                    }
                }

                RowLayout {
                    width: parent.width
                    spacing: Theme.spacingMd
                    visible: root.rateControl === "bitrate" && root.videoSupportsBitrate

                    ThemedLabel {
                        text: qsTr("Bitrate (kbps)")
                        size: "sm"
                    }

                    ThemedNumberField {
                        Layout.preferredWidth: 120
                        from: 100
                        to: 100000
                        step: 100
                        unit: qsTr("kbps")
                        value: root.videoBitrateKbps
                        onEdited: function (v) { root.videoBitrateKbps = Math.round(v) }
                    }
                }
            }

            // Preset
            Column {
                width: parent.width
                spacing: Theme.spacingSm
                visible: root.videoSupportsPreset && root.videoPresetList.length > 0

                ThemedLabel {
                    text: qsTr("Preset")
                    size: "sm"
                }

                ThemedComboBox {
                    id: presetCombo
                    width: parent.width
                    model: root.videoPresetList
                    onActivated: function (index) {
                        if (index >= 0 && index < root.videoPresetList.length)
                            root.videoPreset = root.videoPresetList[index]
                    }
                }
            }

            // Audio
            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: Theme.spacingLg
                rowSpacing: Theme.spacingSm

                ThemedLabel {
                    text: qsTr("Audio encoder")
                    size: "sm"
                    Layout.fillWidth: true
                }

                ThemedLabel {
                    text: qsTr("Bitrate (kbps)")
                    size: "sm"
                    Layout.fillWidth: true
                    visible: !root.audioLossless
                }

                ThemedComboBox {
                    id: audioCodecCombo
                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "id"
                    model: root.audioCodecs
                    onActivated: function (index) {
                        var item = root.audioCodecs[index]
                        if (!item || !item.available) {
                            currentIndex = Math.max(0, indexOfValue(root.audioCodecId))
                            return
                        }
                        root.audioCodecId = item.id
                        root.refreshCodecMeta()
                    }
                }

                ThemedNumberField {
                    Layout.fillWidth: true
                    visible: !root.audioLossless
                    from: 32
                    to: 512
                    step: 16
                    unit: qsTr("kbps")
                    value: root.audioBitrateKbps
                    onEdited: function (v) { root.audioBitrateKbps = Math.round(v) }
                }
            }
        }
    }
}

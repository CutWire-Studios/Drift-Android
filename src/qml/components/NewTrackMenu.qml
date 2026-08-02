import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dropdown for inserting an empty timeline track by type.
Popup {
    id: root

    width: 180
    padding: Theme.spacingMd
    modal: true
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Arrow keys move the highlight, Enter adds the highlighted track type. The
    // menu used to be mouse-only despite already handling Escape.
    property int highlightIndex: 0

    onOpened: {
        highlightIndex = 0
        // Popup itself is not an Item — Keys.* on the root fails with
        // "Could not attach Keys property". Focus the content column instead.
        contentItem.forceActiveFocus()
    }

    function addHighlighted() {
        if (highlightIndex < 0 || highlightIndex >= trackTypes.length)
            return
        EditorState.addTrack(trackTypes[highlightIndex].type)
        root.close()
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0.0
            to: 1.0
            duration: Theme.durationFast
            easing.type: Theme.easing
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1.0
            to: 0.0
            duration: Theme.durationFast
            easing.type: Theme.easing
        }
    }

    readonly property var trackTypes: [
        { type: "video", label: qsTr("Video"), icon: Theme.icons.film },
        { type: "audio", label: qsTr("Audio"), icon: Theme.icons.music },
        { type: "text", label: qsTr("Text"), icon: Theme.icons.type },
        { type: "subtitle", label: qsTr("Subtitle"), icon: Theme.icons.messageSquare },
        { type: "shape", label: qsTr("Shape"), icon: Theme.icons.shapes },
    ]

    background: Rectangle {
        color: Theme.panelBackground
        border.width: 1
        border.color: Theme.panelBorder
        radius: Theme.radiusMd
    }

    contentItem: Column {
        spacing: 2
        focus: true

        Keys.onUpPressed: function(event) {
            root.highlightIndex = (root.highlightIndex - 1 + root.trackTypes.length)
                                  % root.trackTypes.length
            event.accepted = true
        }
        Keys.onDownPressed: function(event) {
            root.highlightIndex = (root.highlightIndex + 1) % root.trackTypes.length
            event.accepted = true
        }
        Keys.onReturnPressed: function(event) {
            root.addHighlighted()
            event.accepted = true
        }
        Keys.onEnterPressed: function(event) {
            root.addHighlighted()
            event.accepted = true
        }

        Text {
            width: parent.width
            leftPadding: 8
            rightPadding: 8
            topPadding: 4
            bottomPadding: 6
            text: qsTr("New track")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.Medium
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.panelBorder
            opacity: 0.5
        }

        Repeater {
            model: root.trackTypes

            delegate: Rectangle {
                id: trackTypeRow
                required property var modelData
                required property int index

                width: parent.width
                height: Theme.controlHeight + Theme.spacingXs
                radius: Theme.radiusSm
                color: itemMouse.containsMouse || root.highlightIndex === index
                       ? Theme.popoverHover : "transparent"

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingLg
                    anchors.rightMargin: Theme.spacingLg
                    spacing: Theme.spacingLg + Theme.spacingXs

                    IconGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: trackTypeRow.modelData.icon
                        iconSize: Theme.iconSizeMd
                        iconColor: Theme.mutedForeground
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: trackTypeRow.modelData.label
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                }

                MouseArea {
                    id: itemMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: root.highlightIndex = trackTypeRow.index
                    onClicked: {
                        EditorState.addTrack(trackTypeRow.modelData.type)
                        root.close()
                    }
                }
            }
        }
    }
}

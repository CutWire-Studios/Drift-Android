import QtQuick
import QtQuick.Controls.Basic
import Drift

// Themed checkbox. Replaces the raw `CheckBox` used in the settings tab, which
// ignored the theme entirely.
CheckBox {
    id: root

    property string tooltip: ""

    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeXs
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    spacing: Theme.spacingLg

    Accessible.role: Accessible.CheckBox
    Accessible.name: text.length > 0 ? text : tooltip

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: root.leftPadding
        y: parent.height / 2 - height / 2
        radius: Theme.radiusXs
        color: root.checked ? Theme.primary
                            : (root.hovered ? Theme.popoverHover : Theme.panelAccent)
        border.width: Theme.borderWidth
        border.color: root.checked ? Theme.primary : Theme.panelBorder
        opacity: root.enabled ? 1 : 0.5

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        IconGlyph {
            anchors.centerIn: parent
            glyph: Theme.icons.check
            iconSize: Theme.iconSizeSm
            iconColor: Theme.primaryForeground
            visible: root.checked
            opacity: root.checked ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.borderWidthFocus
            radius: Theme.radiusSm
            color: "transparent"
            border.width: Theme.borderWidthFocus
            border.color: Theme.focusRing
            visible: root.visualFocus
        }
    }

    contentItem: Text {
        text: root.text
        font: root.font
        color: Theme.panelForeground
        opacity: root.enabled ? 1 : 0.5
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + root.spacing
        elide: Text.ElideRight
    }

    ThemedToolTip {
        text: root.tooltip
        visible: root.tooltip.length > 0 && (root.hovered || root.visualFocus)
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
}

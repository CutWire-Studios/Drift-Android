import QtQuick
import QtQuick.Controls.Basic
import Drift

// Icon-only button, variants "text" / "ghost" / "secondary".
//
// Built on AbstractButton (not a bare Rectangle) so it joins the tab chain, is
// activatable with Space/Enter, and reports a Button role to accessibility.
// The icon name goes in `glyph` — AbstractButton.icon is FINAL.
AbstractButton {
    id: root

    property string glyph: ""
    property string tooltip: ""
    property real buttonSize: Theme.iconButtonSize
    property real iconSize: Theme.iconSizeBase
    property bool active: false
    // "text": transparent, hover = subtle tint (toolbar buttons)
    // "ghost": transparent, hover = accent background (tab rail, view toggles)
    property string variant: "text"

    implicitWidth: buttonSize
    implicitHeight: buttonSize
    width: buttonSize
    height: buttonSize
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: tooltip.length > 0 ? tooltip : glyph
    Accessible.onPressAction: root.clicked()

    background: Rectangle {
        radius: Theme.radiusSm
        color: {
            if (root.active)
                return Theme.panelSecondaryBg
            if (!root.enabled)
                return "transparent"
            // Both variants now tint on hover. The old "text" variant dimmed to
            // 0.75 opacity, which read as disabled rather than as hover.
            if (root.down)
                return Theme.panelMuted
            if (root.hovered)
                return root.variant === "ghost" ? Theme.panelAccent : Theme.popoverHover
            return "transparent"
        }
        border.width: root.active ? Theme.borderWidth : 0
        border.color: Theme.panelSecondaryBorder
        opacity: root.enabled ? 1 : 0.5

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        // Keyboard focus ring. Drawn outside the fill so it stays visible on the
        // active/selected state too.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: Theme.borderWidthFocus
            border.color: Theme.focusRing
            visible: root.visualFocus
        }
    }

    contentItem: IconGlyph {
        glyph: root.glyph
        iconSize: root.iconSize
        iconColor: root.active ? Theme.panelSecondaryForeground : Theme.mutedForeground
        opacity: root.enabled ? 1 : 0.5

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    ThemedToolTip {
        text: root.tooltip
        // Shown on hover, and on keyboard focus so Tab users get the same labels.
        visible: root.tooltip.length > 0 && (root.hovered || root.visualFocus)
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
}

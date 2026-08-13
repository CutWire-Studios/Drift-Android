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

    // Press feedback — see the note in ThemedButton.qml for why this sits on the
    // root rather than on `background`.
    scale: root.down ? Theme.pressScale : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    Accessible.role: Accessible.Button
    Accessible.name: tooltip.length > 0 ? tooltip : glyph
    Accessible.onPressAction: root.clicked()

    background: Rectangle {
        radius: Theme.radiusSm
        color: {
            // Armed reads as a filled amber pill, not a hue nudge: panelSecondaryBg
            // sits at 1.03:1 on the light panel, so an armed Blade — which splits
            // the next clip you touch — looked identical to an idle button.
            if (root.active)
                return root.down ? Qt.darker(Theme.primary, 1.12) : Theme.primary
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
        border.color: Theme.panelSecondaryForeground
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
        iconColor: root.active ? Theme.primaryForeground : Theme.mutedForeground
        opacity: root.enabled ? 1 : 0.5

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    // Touch has no hover and no Tab focus, so on a phone neither trigger below ever
    // fired: every icon-only control in the shell — seventeen tools in the edit
    // strip alone — carried a written label that could not be reached by any
    // gesture. Long-press reveals it, which is also where Android users look for it.
    property bool _touchTipShown: false

    // Verified on device (Qt 6.11, SM-A566B): a hold emits pressed -> pressAndHold
    // -> released and NO clicked — QQuickAbstractButton's `wasHeld` suppresses it —
    // so peeking at a label never fires the action behind it, Delete included.
    onPressAndHold: {
        if (Theme.touchUi && root.tooltip.length > 0) {
            root._touchTipShown = true
            touchTipTimer.restart()
        }
    }

    Timer {
        id: touchTipTimer
        interval: 2400
        onTriggered: root._touchTipShown = false
    }

    ThemedToolTip {
        text: root.tooltip
        // Shown on hover, on keyboard focus so Tab users get the same labels, and
        // on long-press for touch.
        visible: root.tooltip.length > 0
                 && (root.hovered || root.visualFocus || root._touchTipShown)
        // The hover delay exists to keep dense desktop toolbars quiet. A long-press
        // is already a deliberate 800ms ask, so making it wait again reads as broken.
        delay: root._touchTipShown ? 0 : Theme.tooltipDelay
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
}

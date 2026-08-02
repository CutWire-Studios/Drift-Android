import QtQuick
import QtQuick.Controls.Basic
import Drift

// Basic style's default ScrollBar reads control.palette.dark/mid, which the platform
// theme (e.g. KDE Breeze) populates independently of the QQC2 style. Override both
// visual pieces with Theme colors so it doesn't pick up the desktop color scheme.
ScrollBar {
    id: control

    padding: 2

    contentItem: Rectangle {
        implicitWidth: control.interactive ? 8 : 2
        implicitHeight: control.interactive ? 8 : 2
        radius: Math.min(width, height) / 2
        color: control.pressed ? Theme.mutedForeground : Theme.panelBorder
        opacity: 0.0

        states: State {
            name: "active"
            when: control.policy === ScrollBar.AlwaysOn || (control.active && control.size < 1.0)
            PropertyChanges { control.contentItem.opacity: 0.85 }
        }

        transitions: Transition {
            from: "active"
            SequentialAnimation {
                PauseAnimation { duration: 450 }
                NumberAnimation { target: control.contentItem; duration: 200; property: "opacity"; to: 0.0 }
            }
        }
    }

    // Visible track for always-on bars (e.g. the timeline) so the handle reads as
    // a position indicator; transient bars keep an invisible track.
    background: Rectangle {
        radius: Math.min(width, height) / 2
        color: Theme.panelBorder
        opacity: control.policy === ScrollBar.AlwaysOn ? 0.2 : 0.0
    }
}

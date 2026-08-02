import QtQuick
import QtQuick.Controls.Basic
import Drift

// Themed right-click menu.
//
// The app previously had no context menus at all: right-clicking a clip, track,
// ruler or bookmark did nothing, so several actions were reachable only by
// unlabelled keyboard shortcut or not at all.
//
// Use with ThemedMenuItem for entries.
Menu {
    id: root

    implicitWidth: 200
    padding: Theme.spacingSm
    // Escape closes; clicking outside dismisses.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder
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

    // Styles Action-based entries. Inline MenuItem children bypass this
    // delegate, so declare those as ThemedMenuItem (same styling) instead.
    delegate: ThemedMenuItem {}
}

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

// Themed right-click menu.
//
// The app previously had no context menus at all: right-clicking a clip, track,
// or ruler did nothing, so several actions were reachable only by unlabelled
// keyboard shortcut or not at all. Bookmarks now have a context menu too.
//
// Use with ThemedMenuItem for entries.
Menu {
    id: root

    implicitWidth: 200
    padding: Theme.spacingSm
    // Escape closes; clicking outside dismisses.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Android's Back is not Escape, so the policy above never sees it: Back used to fall
    // through to the shell and pop out of the editor — or quit the app from Home — with the
    // menu still on screen. AndroidMain's Back dispatcher closes the topmost registered menu
    // instead. Held rather than looked up again on close, so a menu that was reparented in
    // between still unregisters.
    property var _backHost: null

    onOpened: {
        root._backHost = Theme.touchUi && root.parent ? root.parent.Window.window : null
        if (root._backHost)
            root._backHost.pushMenu(root)
    }

    onClosed: {
        if (root._backHost)
            root._backHost.popMenu(root)
        root._backHost = null
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder
    }

    // Opacity alone made the menu appear out of nothing; pairing it with a scale
    // gives it presence, matching ThemedDialog. Enter is deliberate, exit snaps.
    // A QML Popup has no transformOrigin, so this scales from the menu's own
    // centre rather than from the click point — close enough at this size.
    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 0.96
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 1.0
                to: 0.96
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
        }
    }

    // Styles Action-based entries. Inline MenuItem children bypass this
    // delegate, so declare those as ThemedMenuItem (same styling) instead.
    delegate: ThemedMenuItem {}
}

import QtQuick
import QtQuick.Controls.Basic
import Drift

// Panel-themed slider that keeps parent Flickables from stealing mouse drags.
Slider {
    id: root

    property var _flickable: null
    property bool _flickableWasInteractive: true

    // Shown in a tooltip while dragging. Set a formatter for units/precision.
    property bool showValueTooltip: true
    property var valueFormatter: function (v) { return Number(v).toFixed(2) }

    // What this slider controls, for assistive tech. The role was already declared
    // but no name ever was, so every instance announced as an unlabelled "slider".
    // Callers should set it; the formatter is a fallback, not a substitute.
    property string label: ""

    // A value worth feeling on the way past: the one the control resets to, and the one people aim
    // for without being able to land on it precisely — 1.0x speed, 100% volume, 0 rotation. NaN,
    // the default, means this slider has no such value and stays quiet between its ends.
    property real detentValue: NaN

    // Feedback is for the drag only. A value arriving from a preset chip, a reset button or the
    // playhead is not a finger reaching anywhere, and the Behaviors above animate the handle across
    // the range on those — which would tick on frames nobody touched.
    // NaN, not `value`: initialising it from value would make this a binding on value, so by the
    // time the handler ran "previous" would already have been updated to "current" and nothing
    // could ever read as a crossing. NaN also stands in for "no previous value yet", which every
    // comparison below is false against.
    property real _lastValue: NaN
    onValueChanged: {
        const previous = _lastValue
        _lastValue = value
        if (!pressed || isNaN(previous))
            return
        const detent = detentValue
        if (!isNaN(detent)
                && ((previous - detent) * (value - detent) < 0
                    || (value === detent && previous !== detent))) {
            Haptics.detent()
            return
        }
        // Reaching an end stops the handle while the finger keeps going — the same "it did not
        // move" a trim runs into at the end of its source, so it gets the same answer.
        if ((value === from && previous !== from) || (value === to && previous !== to))
            Haptics.boundary()
    }

    from: 0
    to: 1
    live: true
    // Every effect and transform parameter is edited through this. 22px is a mouse
    // target; on touch the handle needs a row it can actually be caught in.
    implicitHeight: Theme.touchUi ? 40 : 22
    padding: 0
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.5

    Accessible.role: Accessible.Slider
    Accessible.name: label
    Accessible.description: valueFormatter ? String(valueFormatter(value)) : String(value)

    // Guard against callers that bind inverted ranges (Qt debug asserts in qBound).
    onFromChanged: if (from > to) to = from
    onToChanged: if (to < from) from = to

    function findFlickable(item) {
        var node = item
        while (node) {
            if (node.contentX !== undefined && node.interactive !== undefined && node.contentHeight !== undefined)
                return node
            node = node.parent
        }
        return null
    }

    function restoreFlickable() {
        if (_flickable) {
            _flickable.interactive = _flickableWasInteractive
            _flickable = null
        }
    }

    // Connections (not onPressedChanged) so callers can also handle pressedChanged
    // for preview-drag without replacing this guard. Without it, a destroyed slider
    // can leave the parent Flickable non-interactive.
    Connections {
        target: root
        function onPressedChanged() {
            if (root.pressed) {
                root._flickable = root.findFlickable(root.parent)
                if (root._flickable) {
                    root._flickableWasInteractive = root._flickable.interactive
                    root._flickable.interactive = false
                }
            } else {
                root.restoreFlickable()
            }
        }
    }

    Component.onDestruction: restoreFlickable()

    background: Rectangle {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: Theme.spacingSm
        radius: height / 2
        color: Theme.sliderTrack

        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: Theme.primary

            // Eases programmatic value changes (a preset chip, a reset button)
            // without fighting the drag, which sets position continuously.
            // Must share the handle's curve below, or fill and handle desync.
            Behavior on width {
                enabled: !root.pressed
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingInOut }
            }
        }
    }

    handle: Rectangle {
        id: handleRect
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: Theme.touchUi ? Theme.iconSizeXl : Theme.iconSizeMd
        height: width
        radius: width / 2
        color: root.pressed ? Theme.panelSecondaryForeground
                            : (root.hovered ? Qt.lighter(Theme.primary, 1.25) : Qt.lighter(Theme.primary, 1.15))
        border.width: Theme.borderWidthFocus
        border.color: root.visualFocus ? Theme.focusRing : Theme.primaryForeground
        // Grows slightly on hover so the grab target is legible.
        scale: root.pressed ? 1.1 : (root.hovered || root.visualFocus ? 1.05 : 1.0)

        Behavior on x {
            enabled: !root.pressed
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingInOut }
        }
        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on scale {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    ThemedToolTip {
        parent: root.handle
        visible: root.showValueTooltip && (root.pressed || root.hovered)
        // No reveal delay while dragging — the readout must track the handle.
        delay: root.pressed ? 0 : Theme.tooltipDelay
        text: root.valueFormatter(root.value)
    }

    // Cursor-only overlay. Must forward wheel when inside a scrollable Flickable:
    // MouseArea eats wheel by default, and rejecting it lets Slider::wheelEvent
    // change the value instead of scrolling the panel (Audio/Effects tabs).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => {
            const flick = root.findFlickable(root.parent)
            const maxY = flick ? Math.max(0, flick.contentHeight - flick.height) : 0
            if (!flick || maxY <= 0) {
                wheel.accepted = false
                return
            }
            const dy = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y
                                                : wheel.angleDelta.y / 8
            flick.contentY = Math.max(0, Math.min(maxY, flick.contentY - dy))
            wheel.accepted = true
        }
    }
}

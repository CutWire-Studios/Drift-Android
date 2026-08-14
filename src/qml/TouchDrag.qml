pragma Singleton
import QtQuick
import Drift

// Lift-and-drop for the phone shell's asset sheet.
//
// Qt's Drag.Automatic is a platform drag: there is no touch gesture that starts
// one on Android, and it cannot cross a modal Popup — which is exactly what the
// asset sheet is. So the browsers lift their cards into this instead. The item
// that started the gesture keeps the touch grab and feeds coordinates in here,
// the sheet steps aside rather than closing (closing would destroy the grab
// mid-flight), and the timeline registers itself as the drop target and does its
// own hit-testing on release.
QtObject {
    id: touchDrag

    // "" while idle. Otherwise one of media | effect | audioEffect | template |
    // transition — the drop target switches on this.
    property string kind: ""
    // Asset index for "media", a catalog id for every other kind.
    property var payload: null
    property string label: ""
    // Ghost art: an image source when the card has a thumbnail, a glyph otherwise.
    property string thumbnail: ""
    property string glyph: ""

    readonly property bool active: kind !== ""

    // Finger position, in scene coordinates.
    property real sceneX: 0
    property real sceneY: 0

    // The timeline registers itself here; it owns the landing preview and the drop.
    property var dropTarget: null
    // True while the finger is over a spot the drop target would accept, so the
    // ghost can say so.
    property bool overTarget: false
    // Set by the sheet once the finger has left it. Until then the drop target is
    // still behind the sheet, and letting go there would land a clip at a spot the
    // sheet is covering — nothing anyone aimed at.
    property bool clearOfSource: false

    function begin(dragKind, dragPayload, opts) {
        const o = opts || {}
        kind = dragKind
        payload = dragPayload
        label = o.label || ""
        thumbnail = o.thumbnail || ""
        glyph = o.glyph || ""
        overTarget = false
        clearOfSource = false
        // The timeline's new-track ghost sizes itself from the dragged asset's
        // track type, which it reads off here exactly as the desktop drop does.
        if (dragKind === "media")
            EditorState.draggingAssetIndex = dragPayload
    }

    function moveTo(x, y) {
        sceneX = x
        sceneY = y
        if (!active || !dropTarget)
            return
        // clearOfSource is latched by the sheet from its own sceneY handler, so it
        // is already up to date for this position.
        if (!clearOfSource) {
            overTarget = false
            dropTarget.clearTouchDrop()
            return
        }
        overTarget = dropTarget.updateTouchDrop(kind, payload, x, y)
    }

    function finish() {
        if (!active)
            return
        if (dropTarget) {
            if (clearOfSource)
                dropTarget.performTouchDrop(kind, payload, sceneX, sceneY)
            else
                dropTarget.clearTouchDrop()
        }
        _reset()
    }

    function cancel() {
        if (!active)
            return
        if (dropTarget)
            dropTarget.clearTouchDrop()
        _reset()
    }

    function _reset() {
        kind = ""
        payload = null
        label = ""
        thumbnail = ""
        glyph = ""
        overTarget = false
        clearOfSource = false
        EditorState.draggingAssetIndex = -1
    }
}

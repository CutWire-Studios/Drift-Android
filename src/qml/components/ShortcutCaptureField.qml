import QtQuick
import QtQuick.Controls.Basic

// Click to arm, then press a chord (modifiers + key). Esc cancels; Backspace clears.
Rectangle {
    id: root

    property string actionId
    property string shortcut
    property bool capturing: false

    width: 120
    height: 26
    radius: Theme.radiusSm
    color: capturing ? Theme.panelSecondaryBg : Theme.panelAccent
    border.width: 1
    border.color: capturing ? Theme.primary : Theme.panelBorder

    function keyName(key) {
        const named = ({
            0x01000000: "Escape",
            0x01000003: "Backspace",
            0x01000004: "Return",
            0x01000005: "Enter",
            0x01000006: "Insert",
            0x01000007: "Delete",
            0x01000010: "Home",
            0x01000011: "End",
            0x01000012: "Left",
            0x01000013: "Up",
            0x01000014: "Right",
            0x01000015: "Down",
            0x01000016: "PageUp",
            0x01000017: "PageDown",
            0x01000020: "Shift",
            0x01000021: "Control",
            0x01000023: "Alt",
            0x01000024: "Meta",
            0x20: "Space",
            0x09: "Tab"
        })
        if (named[key])
            return named[key]
        if (key >= Qt.Key_F1 && key <= Qt.Key_F12)
            return "F" + (key - Qt.Key_F1 + 1)
        if (key >= Qt.Key_0 && key <= Qt.Key_9)
            return String.fromCharCode(key)
        if (key >= Qt.Key_A && key <= Qt.Key_Z)
            return String.fromCharCode(key)
        return ""
    }

    function chordFromEvent(event) {
        const name = keyName(event.key)
        if (!name || name === "Shift" || name === "Control" || name === "Alt" || name === "Meta")
            return ""

        let parts = []
        if (event.modifiers & Qt.ControlModifier)
            parts.push("Ctrl")
        if (event.modifiers & Qt.AltModifier)
            parts.push("Alt")
        if (event.modifiers & Qt.ShiftModifier)
            parts.push("Shift")
        if (event.modifiers & Qt.MetaModifier)
            parts.push("Meta")
        parts.push(name)
        return parts.join("+")
    }

    Text {
        anchors.fill: parent
        anchors.margins: 4
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: capturing ? "Press keys…" : (shortcut.length > 0 ? shortcut : "Click to set")
        color: capturing ? Theme.primary : Theme.panelForeground
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.capturing = true
            captureFocus.forceActiveFocus()
        }
    }

    Item {
        id: captureFocus
        anchors.fill: parent
        focus: false
        Keys.onPressed: function(event) {
            if (!root.capturing)
                return

            if (event.key === Qt.Key_Escape) {
                root.capturing = false
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                EditorState.setShortcut(root.actionId, "")
                root.capturing = false
                event.accepted = true
                return
            }

            const chord = root.chordFromEvent(event)
            if (chord.length === 0)
                return

            EditorState.setShortcut(root.actionId, chord)
            root.capturing = false
            event.accepted = true
        }
        onActiveFocusChanged: {
            if (!activeFocus && root.capturing)
                root.capturing = false
        }
    }
}

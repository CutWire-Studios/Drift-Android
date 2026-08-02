import QtQuick
import QtQuick.Window
import Drift 1.0
import "components"

// Unit gain curve editor for clip edge fades. X = progress through the fade,
// Y = gain (silent at the bottom, full at the top). Shared by fade-in and fade-out.
Window {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property var points: []
    property int selectedPoint: -1
    property bool closingAfterApply: false

    width: 520
    height: 420
    minimumWidth: 420
    minimumHeight: 340
    title: qsTr("Custom curve")
    color: Theme.appBackground

    // Android hands a secondary top-level window the whole display and gives it no
    // frame, so the desktop size above would leave it laid out for a screen it does
    // not have. Asked for on show rather than bound, because show() drives visibility
    // itself and would overwrite a binding.
    onVisibleChanged: if (visible && Qt.platform.os === "android") visibility = Window.FullScreen

    function openFor(track, clip) {
        root.trackIndex = track
        root.clipIndex = clip
        root.selectedPoint = -1
        root.closingAfterApply = false
        EditorState.beginFadeCurveSession(track, clip)
        root.points = EditorState.fadeCurvePoints
        root.ensureEditable()
        root.show()
        root.raise()
        root.requestActivate()
        curveCanvas.requestPaint()
    }

    onClosing: {
        if (!root.closingAfterApply)
            EditorState.endFadeCurveSession()
    }

    Connections {
        target: EditorState
        function onFadeCurveApplied() {
            root.closingAfterApply = true
            root.close()
        }
    }

    function gainAt(t) {
        const pts = root.points
        if (!pts || pts.length < 2)
            return t
        t = Math.max(0, Math.min(1, t))
        if (t <= pts[0].t)
            return pts[0].g
        if (t >= pts[pts.length - 1].t)
            return pts[pts.length - 1].g
        for (let i = 0; i + 1 < pts.length; ++i) {
            const a = pts[i]
            const b = pts[i + 1]
            if (t < a.t || t > b.t)
                continue
            const span = b.t - a.t
            if (Math.abs(span) < 1e-9)
                return b.g
            const u = (t - a.t) / span
            return a.g + (b.g - a.g) * u
        }
        return pts[pts.length - 1].g
    }

    function clonePoints() {
        return root.points.map(function (p) {
            return { t: p.t, g: p.g }
        })
    }

    // Push the local mirror into the session. `resync` re-reads the clamped/sorted
    // points back from C++ — only safe on drag release, or the knob will fight the
    // controller for the same values mid-drag.
    function commit(resync) {
        EditorState.setFadeCurvePoints(root.points)
        if (resync !== false) {
            root.points = EditorState.fadeCurvePoints
            curveCanvas.requestPaint()
        }
    }

    function addPointAt(t) {
        t = Math.max(0.02, Math.min(0.98, t))
        const next = root.clonePoints()
        // Don't stack on top of an existing knot.
        for (let i = 0; i < next.length; ++i) {
            if (Math.abs(next[i].t - t) < 0.02)
                return
        }
        const point = { t: t, g: root.gainAt(t) }
        let index = next.length - 1
        for (let i = 0; i < next.length; ++i) {
            if (next[i].t > t) {
                index = i
                break
            }
        }
        next.splice(index, 0, point)
        root.points = next
        root.selectedPoint = index
        root.commit()
    }

    // If a preset left only the two rails, drop a middle handle so the curve is editable.
    function ensureEditable() {
        if (root.points.length >= 3)
            return
        root.addPointAt(0.5)
    }

    function removeSelected() {
        if (root.selectedPoint <= 0 || root.selectedPoint >= root.points.length - 1)
            return
        const next = root.clonePoints()
        next.splice(root.selectedPoint, 1)
        root.points = next
        root.selectedPoint = -1
        root.commit()
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingXl
        spacing: Theme.spacingLg

        Row {
            width: parent.width
            spacing: Theme.spacingMd

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: EditorState.fadeCurveClipName.length
                      ? qsTr("Fade shape — %1").arg(EditorState.fadeCurveClipName)
                      : qsTr("Fade shape")
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.Medium
                elide: Text.ElideRight
                width: Math.max(80, parent.width - presetRow.width - parent.spacing)
            }

            Row {
                id: presetRow
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter

                ThemedChip {
                    text: qsTr("Linear")
                    onClicked: {
                        EditorState.resetFadeCurvePreset("linear")
                        root.points = EditorState.fadeCurvePoints
                        root.selectedPoint = -1
                        root.ensureEditable()
                        curveCanvas.requestPaint()
                    }
                }
                ThemedChip {
                    text: qsTr("Smooth")
                    onClicked: {
                        EditorState.resetFadeCurvePreset("smooth")
                        root.points = EditorState.fadeCurvePoints
                        root.selectedPoint = -1
                        curveCanvas.requestPaint()
                    }
                }
                ThemedChip {
                    text: qsTr("Natural")
                    onClicked: {
                        EditorState.resetFadeCurvePreset("equalPower")
                        root.points = EditorState.fadeCurvePoints
                        root.selectedPoint = -1
                        curveCanvas.requestPaint()
                    }
                }
            }
        }

        Text {
            width: parent.width
            text: qsTr("Drag the middle points to shape the ramp (ends stay silent→full). Double-click to add a point; Delete removes the selection.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            wrapMode: Text.WordWrap
        }

        Item {
            id: plot
            width: parent.width
            height: Math.max(180, root.height - 200)

            readonly property real inset: 16
            readonly property real plotW: Math.max(1, width - inset * 2)
            readonly property real plotH: Math.max(1, height - inset * 2)

            function xForT(t) { return inset + t * plotW }
            function yForG(g) { return inset + (1.0 - g) * plotH }
            function tForX(x) { return (x - inset) / plotW }
            function gForY(y) { return 1.0 - (y - inset) / plotH }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusMd
                color: Theme.panelAccent
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder
            }

            Canvas {
                id: curveCanvas
                anchors.fill: parent
                anchors.margins: plot.inset
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const w = width
                    const h = height
                    if (w < 2 || h < 2)
                        return

                    ctx.strokeStyle = String(Theme.panelBorder)
                    ctx.lineWidth = 1
                    ctx.globalAlpha = 0.45
                    for (let i = 1; i < 4; ++i) {
                        const y = h * i / 4
                        ctx.beginPath()
                        ctx.moveTo(0, y)
                        ctx.lineTo(w, y)
                        ctx.stroke()
                        const x = w * i / 4
                        ctx.beginPath()
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, h)
                        ctx.stroke()
                    }
                    ctx.globalAlpha = 1

                    const pts = root.points
                    if (!pts || pts.length < 2)
                        return

                    const fill = Theme.primary
                    ctx.fillStyle = Qt.rgba(fill.r, fill.g, fill.b, 0.18)
                    ctx.beginPath()
                    ctx.moveTo(0, h)
                    for (let i = 0; i < pts.length; ++i)
                        ctx.lineTo(pts[i].t * w, h * (1.0 - pts[i].g))
                    ctx.lineTo(w, h)
                    ctx.closePath()
                    ctx.fill()

                    ctx.strokeStyle = String(Theme.primary)
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    for (let i = 0; i < pts.length; ++i) {
                        const x = pts[i].t * w
                        const y = h * (1.0 - pts[i].g)
                        if (i === 0)
                            ctx.moveTo(x, y)
                        else
                            ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                }

                Connections {
                    target: root
                    function onPointsChanged() { curveCanvas.requestPaint() }
                    function onSelectedPointChanged() { curveCanvas.requestPaint() }
                }
            }

            // Background click/double-click under the knobs. Knobs use z:4 so they win the grab.
            MouseArea {
                anchors.fill: parent
                anchors.margins: plot.inset
                z: 1
                acceptedButtons: Qt.LeftButton
                onDoubleClicked: (mouse) => root.addPointAt(mouse.x / plot.plotW)
                onClicked: root.selectedPoint = -1
            }

            // model is the count, not the array — replacing points mid-drag must not destroy the
            // knob that owns the active DragHandler (that was making manual edits jump/stick).
            Repeater {
                model: root.points.length

                delegate: Rectangle {
                    id: knob
                    required property int index
                    readonly property var point: root.points[index]
                    readonly property bool isEnd: index === 0 || index === root.points.length - 1

                    width: 14
                    height: 14
                    radius: 7
                    z: 4
                    color: root.selectedPoint === index ? Theme.primary : Theme.panelBackground
                    border.width: 2
                    border.color: Theme.primary
                    x: plot.xForT(point.t) - width / 2
                    y: plot.yForG(point.g) - height / 2

                    // DragHandler reports translation from press; freeze the starting t/g so each
                    // move is absolute from that origin instead of compounding.
                    property real baseT: 0
                    property real baseG: 0

                    TapHandler {
                        onTapped: root.selectedPoint = knob.index
                    }

                    DragHandler {
                        // Corner rails stay at (0,0) and (1,1); only interior knobs move.
                        enabled: !knob.isEnd
                        target: null
                        cursorShape: Qt.PointingHandCursor
                        onActiveChanged: {
                            if (active) {
                                knob.baseT = knob.point.t
                                knob.baseG = knob.point.g
                                root.selectedPoint = knob.index
                            } else {
                                root.commit()
                            }
                        }
                        onTranslationChanged: {
                            if (!active)
                                return
                            const next = root.clonePoints()
                            const point = next[knob.index]
                            const lo = next[knob.index - 1].t + 0.002
                            const hi = next[knob.index + 1].t - 0.002
                            point.t = Math.max(lo, Math.min(hi,
                                knob.baseT + translation.x / plot.plotW))
                            point.g = Math.max(0, Math.min(1,
                                knob.baseG - translation.y / plot.plotH))
                            root.points = next
                            // Live audition without round-tripping points (keeps the drag smooth).
                            root.commit(false)
                        }
                    }
                }
            }

            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                    root.removeSelected()
                    event.accepted = true
                }
            }
            focus: true
        }

        Row {
            width: parent.width
            spacing: Theme.spacingMd
            layoutDirection: Qt.RightToLeft

            ThemedButton {
                text: qsTr("Apply")
                onClicked: EditorState.applyFadeCurve()
            }
            ThemedButton {
                text: qsTr("Cancel")
                variant: "ghost"
                onClicked: root.close()
            }
            Item { width: parent.width; height: 1 }
        }
    }
}

import QtQuick
import QtQuick.Controls.Basic
import Drift

// Standard "nothing here yet" placeholder: icon frame, title, hint, optional CTA.
//
// Generalized from the one good hand-built instance in PropertiesPanel so that
// empty media grids, empty timelines, empty tabs and empty lists all read the
// same instead of being variously a rich card, plain centered text, or nothing.
Item {
    id: root

    property string glyph: Theme.icons.info
    // Set alongside a loader glyph so it actually turns.
    property bool glyphSpinning: false
    property string title: ""
    property string hint: ""
    // Leave empty to omit the button.
    property string actionText: ""
    property string actionVariant: "secondary"
    // Compact drops the icon frame — for small panes where it would dominate.
    property bool compact: false

    signal actionTriggered()

    implicitWidth: Math.min(260, parent ? parent.width : 260)
    // Height follows the column directly — anchors.centerIn on a child whose
    // parent sizes from that child's implicitHeight is circular and can collapse.
    width: parent ? parent.width : implicitWidth
    height: column.height
    implicitHeight: column.height

    Column {
        id: column
        width: Math.min(root.width, 260)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.spacingLg

        Rectangle {
            visible: !root.compact
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48
            height: 48
            radius: Theme.radiusMd
            color: Theme.panelAccent

            IconGlyph {
                anchors.centerIn: parent
                glyph: root.glyph
                iconSize: Theme.iconSizeXl
                iconColor: Theme.mutedForeground
                spinning: root.glyphSpinning
            }
        }

        Text {
            visible: root.title.length > 0
            width: parent.width
            text: root.title
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            visible: root.hint.length > 0
            width: parent.width
            text: root.hint
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        ThemedButton {
            visible: root.actionText.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.actionText
            variant: root.actionVariant
            onClicked: root.actionTriggered()
        }
    }
}

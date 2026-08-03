import QtQuick
import Drift

// Reusable inspector block: optional chevron collapse, optional enable switch,
// hairline separator, and a default-property content slot.
//
// Content is shown when the section is expanded, and — if `showSwitch` — only
// while the switch is on (so feature groups like Shadow hide their knobs when off).
Item {
    id: root

    property string title: ""
    property string tooltip: ""

    // Major sections (Style, Appearance, …) use the chevron. Nested feature
    // rows (Outline, Shadow, …) usually set collapsible: false and showSwitch.
    property bool collapsible: true
    property bool expanded: true
    property bool showSeparator: true

    property bool showSwitch: false
    property bool switchChecked: false
    property string switchTooltip: ""

    signal switchToggled(bool checked)

    default property alias content: body.data

    readonly property bool contentVisible: (!collapsible || expanded)
                                           && (!showSwitch || switchChecked)

    width: parent ? parent.width : implicitWidth
    implicitWidth: 200
    implicitHeight: column.height

    Column {
        id: column
        width: parent.width
        spacing: 0

        Rectangle {
            width: parent.width
            height: Theme.borderWidth
            visible: root.showSeparator
            color: Theme.panelBorder
        }

        Item {
            id: header
            width: parent.width
            height: Math.max(Theme.controlHeightSm,
                             titleLabel.implicitHeight,
                             root.showSwitch ? 20 : 0) + Theme.spacingMd

            Row {
                id: titleRow
                anchors.left: parent.left
                anchors.right: root.showSwitch ? switchControl.left : parent.right
                anchors.rightMargin: root.showSwitch ? Theme.spacingSm : 0
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: root.showSeparator ? Theme.spacingMd / 2 : 0
                spacing: Theme.spacingSm

                Item {
                    width: root.collapsible ? 14 : 0
                    height: 14
                    visible: root.collapsible
                    anchors.verticalCenter: parent.verticalCenter

                    IconGlyph {
                        anchors.centerIn: parent
                        glyph: root.expanded ? Theme.icons.chevronDown : Theme.icons.chevronRight
                        iconSize: 12
                        iconColor: Theme.mutedForeground
                    }
                }

                Text {
                    id: titleLabel
                    width: Math.max(0, titleRow.width - (root.collapsible ? 14 + titleRow.spacing : 0))
                    text: root.title
                    elide: Text.ElideRight
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.DemiBold
                    anchors.verticalCenter: parent.verticalCenter

                    HoverHandler { id: titleHover }
                    ThemedToolTip {
                        text: root.tooltip
                        visible: root.tooltip.length > 0 && titleHover.hovered
                    }
                }
            }

            MouseArea {
                anchors.fill: titleRow
                enabled: root.collapsible
                cursorShape: root.collapsible ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.expanded = !root.expanded
            }

            ThemedSwitch {
                id: switchControl
                anchors.right: parent.right
                anchors.verticalCenter: titleRow.verticalCenter
                visible: root.showSwitch
                checked: root.switchChecked
                tooltip: root.switchTooltip
                // Title already names the feature; keep only the track visible.
                text: ""
                width: 36
                clip: true
                onToggled: root.switchToggled(checked)
            }
        }

        Column {
            id: body
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.contentVisible
            topPadding: visible ? Theme.spacingSm : 0
            bottomPadding: visible ? Theme.spacingSm : 0
        }
    }
}

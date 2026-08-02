import QtQuick
import QtQuick.Controls.Basic
import Drift
import "components"

// CapCut-style bottom tool rail — library tabs + Edit.
Item {
    id: root

    // "media" | "sounds" | ... | "settings" | "shortcuts" | "edit" | ""
    property string activeId: ""
    signal tabRequested(string tabId)
    signal editRequested()

    // Clear the system gesture/nav bar without leaving a dead strip under the rail.
    readonly property real bottomInset: SafeArea.margins.bottom

    height: Theme.androidBottomRailHeight + bottomInset
    width: parent ? parent.width : 0

    readonly property var items: [
        { id: "media", label: qsTr("Media"), icon: Theme.icons.film },
        { id: "sounds", label: qsTr("Sounds"), icon: Theme.icons.headphones },
        { id: "text", label: qsTr("Text"), icon: Theme.icons.type },
        { id: "stickers", label: qsTr("Stickers"), icon: Theme.icons.smile },
        { id: "shapes", label: qsTr("Shapes"), icon: Theme.icons.shapes },
        { id: "effects", label: qsTr("Effects"), icon: Theme.icons.wand },
        { id: "templates", label: qsTr("Templates"), icon: Theme.icons.layers },
        { id: "transitions", label: qsTr("Transitions"), icon: Theme.icons.blend },
        { id: "edit", label: qsTr("Edit"), icon: Theme.icons.sliders },
        { id: "settings", label: qsTr("Settings"), icon: Theme.icons.settings },
        { id: "shortcuts", label: qsTr("Keys"), icon: Theme.icons.keyboard }
    ]

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBackground
    }

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Theme.panelBorder
        z: 1
    }

    Flickable {
        id: flick
        // Extra top inset so icons aren't tight under the hairline; bottom uses the
        // safe-area inset only (no second empty pad under the rail).
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.spacingMd + 1
        anchors.bottomMargin: root.bottomInset
        contentWidth: railRow.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: railRow
            height: parent.height
            leftPadding: Theme.spacingSm
            rightPadding: Theme.spacingSm
            spacing: 2

            Repeater {
                model: root.items

                delegate: AbstractButton {
                    id: railBtn
                    required property var modelData
                    width: Theme.androidRailItemWidth
                    height: parent.height
                    hoverEnabled: true

                    readonly property bool selected: root.activeId === modelData.id

                    background: Item { }

                    contentItem: Column {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        IconGlyph {
                            anchors.horizontalCenter: parent.horizontalCenter
                            glyph: railBtn.modelData.icon
                            iconSize: Theme.iconSizeBase
                            iconColor: railBtn.selected ? Theme.primary : Theme.mutedForeground
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: railBtn.modelData.label
                            color: railBtn.selected ? Theme.primary : Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeTick
                            font.weight: railBtn.selected ? Font.Medium : Font.Normal
                        }
                    }

                    onClicked: {
                        if (modelData.id === "edit")
                            root.editRequested()
                        else
                            root.tabRequested(modelData.id)
                    }
                }
            }
        }
    }
}

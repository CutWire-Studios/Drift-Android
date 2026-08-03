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
    // The side insets matter in landscape, where a cutout or the gesture pill sits
    // beside the rail and would otherwise swallow the first and last tabs.
    readonly property real bottomInset: SafeArea.margins.bottom
    readonly property real leftInset: SafeArea.margins.left
    readonly property real rightInset: SafeArea.margins.right

    height: Theme.androidBottomRailHeight + bottomInset
    width: parent ? parent.width : 0

    // Ids, labels and icons mirror AssetsPanel.qml's tabsModel so the rail and the sheet
    // header cannot disagree; "edit" is the extra Android entry that opens PropertiesPanel
    // and is placed second because clip editing is the most-used destination here.
    readonly property var items: [
        { id: "media", label: qsTr("Media"), icon: Theme.icons.film },
        { id: "edit", label: qsTr("Edit"), icon: Theme.icons.sliders },
        { id: "text", label: qsTr("Text"), icon: Theme.icons.type },
        { id: "subtitles", label: qsTr("Subtitles"), icon: Theme.icons.captions },
        { id: "stickers", label: qsTr("Stickers"), icon: Theme.icons.smile },
        { id: "shapes", label: qsTr("Shapes"), icon: Theme.icons.shapes },
        { id: "effects", label: qsTr("Effects"), icon: Theme.icons.wand },
        { id: "templates", label: qsTr("Templates"), icon: Theme.icons.layers },
        { id: "transitions", label: qsTr("Transitions"), icon: Theme.icons.chevronsRight },
        { id: "sounds", label: qsTr("Audio FX"), icon: Theme.icons.audioLines },
        // No "shortcuts" entry: every row in that tab is a hardware-key capture field,
        // and the shortcuts it edits drive Shortcut bindings the touch shell never
        // instantiates. The tab itself stays for desktop; the rail simply omits it.
        { id: "settings", label: qsTr("Settings"), icon: Theme.icons.settings }
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
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset
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

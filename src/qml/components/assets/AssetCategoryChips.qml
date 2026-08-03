import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Category filter row: favorites star, then horizontal category chips.
// ⭐ | [Color] [Glitch] …
Item {
    id: root

    readonly property string favoritesId: "__favorites__"

    property var categories: []
    property string activeCategory: ""
    property bool searching: false

    signal categoryActivated(string categoryId)

    width: parent ? parent.width : 0
    height: visible ? Theme.controlHeightSm : 0
    visible: !searching && categories.length > 0

    Row {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        spacing: Theme.spacingSm

        IconButton {
            id: favButton
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.star
            variant: "ghost"
            buttonSize: Theme.controlHeightSm
            iconSize: 14
            active: root.activeCategory === root.favoritesId
            tooltip: qsTr("Favorites")
            onClicked: root.categoryActivated(root.favoritesId)
        }

        Rectangle {
            id: divider
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.borderWidth
            height: Theme.controlHeightSm - 6
            color: Theme.panelBorder
        }

        Flickable {
            width: Math.max(0, row.width - favButton.width - divider.width - row.spacing * 2)
            height: parent.height
            contentWidth: categoryRow.width
            flickableDirection: Flickable.HorizontalFlick
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentWidth > width

            Row {
                id: categoryRow
                height: parent.height
                spacing: Theme.spacingSm

                Repeater {
                    model: root.categories
                    delegate: ThemedChip {
                        required property var modelData
                        required property int index
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        variant: "secondary"
                        accentColor: Theme.categoryColor(index)
                        selected: root.activeCategory === modelData.id
                        onClicked: root.categoryActivated(modelData.id)
                    }
                }
            }
        }
    }
}

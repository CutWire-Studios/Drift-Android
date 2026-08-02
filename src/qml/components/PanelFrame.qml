import QtQuick
import Drift

// Shared ".panel" chrome: dark surface, hairline border, 5.6px radius.
Rectangle {
    default property alias content: contentItem.data

    color: Theme.panelBackground
    border.color: Theme.panelBorder
    border.width: 1
    radius: Theme.radiusSm
    clip: true

    Item {
        id: contentItem
        anchors.fill: parent
    }
}

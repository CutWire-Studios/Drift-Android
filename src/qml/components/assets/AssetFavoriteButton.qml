import QtQuick
import Drift
import ".."

// Star toggle for marking an asset-browser item as a favorite.
IconButton {
    id: root

    property string tabId: ""
    property string itemId: ""

    property bool favorited: false

    glyph: Theme.icons.star
    variant: "ghost"
    // The star sits in the corner of a card whose whole face is a tap target, so at
    // 18px on touch it is both hard to hit and easy to hit by accident. The glyph
    // stays small; only the target grows.
    buttonSize: Theme.touchUi ? 32 : 18
    iconSize: Theme.touchUi ? 14 : 12
    active: favorited
    tooltip: favorited ? qsTr("Remove from favorites") : qsTr("Add to favorites")

    Component.onCompleted: favorited = EditorState.isAssetFavorite(tabId, itemId)

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() {
            root.favorited = EditorState.isAssetFavorite(root.tabId, root.itemId)
        }
    }

    onClicked: EditorState.toggleAssetFavorite(tabId, itemId)
}

import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Shortcuts tab. Split out of Settings, which had grown long
// enough that the binding list was permanently below the fold.
Item {
    id: root

    Flickable {
        anchors.fill: parent
        contentHeight: shortcutColumn.height + Theme.spacing3xl
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: shortcutColumn
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            spacing: Theme.spacingMd
            topPadding: Theme.pagePadding

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Click a shortcut, then press the keys. Esc cancels, Backspace clears.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                bottomPadding: Theme.spacingSm
            }

            EmptyState {
                visible: !EditorState.actions || EditorState.actions.length === 0
                width: parent.width
                compact: true
                glyph: Theme.icons.keyboard
                title: qsTr("No shortcuts available")
            }

            Repeater {
                model: EditorState.actions
                delegate: Row {
                    required property var modelData
                    width: shortcutColumn.width
                    spacing: Theme.spacingLg

                    Text {
                        width: Math.max(90, shortcutColumn.width - 128)
                        text: modelData.label
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        wrapMode: Text.WordWrap
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ShortcutCaptureField {
                        width: 120
                        actionId: modelData.id
                        shortcut: modelData.shortcut
                    }
                }
            }
        }
    }
}

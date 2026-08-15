import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Text tab: click a style pack to drop a styled text clip on the timeline
// (placeholder copy + inline edit). Timed captions live under Subtitles.
Item {
    id: root

    // A clip landed on the timeline. The phone shell closes the sheet on this —
    // the thing you came for is behind it.
    signal added()

    readonly property var presets: EditorState.textPresets()

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: textColumn.height + Theme.spacing3xl
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: textColumn
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            spacing: Theme.spacingMd
            topPadding: Theme.pagePadding

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Click a style to add text at the playhead. Double-click it on the preview to edit.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Grid {
                id: packGrid
                width: parent.width
                columns: Math.max(1, Math.floor((width + Theme.assetCardGap)
                                                / (Theme.assetCardWidth + Theme.assetCardGap)))
                columnSpacing: Theme.assetCardGap
                rowSpacing: Theme.assetCardGap

                Repeater {
                    model: root.presets
                    delegate: Column {
                        id: packCard
                        required property var modelData
                        width: Theme.assetCardWidth
                        spacing: Theme.spacingSm

                        scale: packPress.pressed ? 0.97 : (packHover.hovered ? 1.02 : 1.0)
                        Behavior on scale {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        TextStylePackThumb {
                            width: parent.width
                            height: Math.round(width * 0.55)
                            presetId: packCard.modelData.id
                            hovered: packHover.hovered

                            HoverHandler {
                                id: packHover
                            }

                            TapHandler {
                                id: packPress
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: {
                                    EditorState.addTextClip("", -1, packCard.modelData.id)
                                    root.added()
                                }
                            }
                        }

                        Text {
                            width: parent.width
                            text: packCard.modelData.label
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            color: packHover.hovered ? Theme.panelForeground : Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs

                            Behavior on color {
                                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }
                        }
                    }
                }
            }
        }
    }
}

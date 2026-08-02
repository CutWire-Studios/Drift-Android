import QtQuick
import Drift

// Sounds tab: audio-effect preset library (browse left, edit in the Audio inspector).
Item {
    id: root

    AudioEffectBrowser {
        anchors.fill: parent
    }
}

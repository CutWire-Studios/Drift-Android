import QtQuick
import Drift

// Shimmering placeholder for content that is still being produced: async
// thumbnails, filmstrip frames, waveform peaks, addon lists.
//
// Previously these surfaces were simply blank while loading, which was
// indistinguishable from "failed" or "empty".
Rectangle {
    id: root

    // Set false to freeze the shimmer (e.g. off-screen delegates).
    property bool animated: true
    property real shimmerWidth: 80

    color: Theme.skeletonColor
    radius: Theme.radiusSm
    clip: true

    Rectangle {
        id: shimmer
        width: root.shimmerWidth
        height: parent.height * 2
        rotation: 18
        anchors.verticalCenter: parent.verticalCenter
        opacity: 0.55

        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.skeletonHighlight }
            GradientStop { position: 1.0; color: "transparent" }
        }

        XAnimator on x {
            running: root.animated && root.visible && root.width > 0
            loops: Animation.Infinite
            from: -shimmer.width
            to: root.width + shimmer.width
            duration: 1200
            easing.type: Easing.InOutQuad
        }
    }
}

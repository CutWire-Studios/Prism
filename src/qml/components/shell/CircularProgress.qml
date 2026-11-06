import QtQuick
import Prism

// Small circular progress ring drawn on a Canvas. value is 0..1.
//
// Set `indeterminate` for phases that report no fraction (an install that has
// started but not yet published progress); a value of 0 otherwise paints an
// empty ring that is indistinguishable from "not started".
Item {
    id: root

    property real value: 0
    property bool indeterminate: false
    property real size: 28
    property real strokeWidth: 3
    property color trackColor: Theme.c.circularProgress.track
    property color progressColor: Theme.c.circularProgress.progress

    width: size
    height: size

    Canvas {
        id: canvas
        anchors.fill: parent

        // Sweeps continuously while indeterminate.
        property real spin: 0

        NumberAnimation on spin {
            running: root.indeterminate && root.visible
            loops: Animation.Infinite
            from: 0
            to: 360
            duration: 1100
        }

        onSpinChanged: if (root.indeterminate) requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()

            var cx = width / 2
            var cy = height / 2
            var radius = Math.min(width, height) / 2 - root.strokeWidth / 2

            // Indeterminate draws a fixed-length arc that rotates; determinate
            // draws from 12 o'clock to the current fraction.
            var start = root.indeterminate
                    ? (canvas.spin * Math.PI / 180) - Math.PI / 2
                    : -Math.PI / 2
            var sweep = root.indeterminate
                    ? Math.PI * 0.6
                    : Math.max(0, Math.min(1, root.value)) * Math.PI * 2
            var end = start + sweep

            ctx.lineWidth = root.strokeWidth
            ctx.lineCap = "round"

            ctx.strokeStyle = root.trackColor
            ctx.beginPath()
            ctx.arc(cx, cy, radius, 0, Math.PI * 2, false)
            ctx.stroke()

            ctx.strokeStyle = root.progressColor
            ctx.beginPath()
            ctx.arc(cx, cy, radius, start, end, false)
            ctx.stroke()
        }
    }

    // Eases determinate progress so the ring sweeps rather than steps.
    Behavior on value {
        enabled: !root.indeterminate
        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
    }

    onValueChanged: canvas.requestPaint()
    onIndeterminateChanged: canvas.requestPaint()
    onSizeChanged: canvas.requestPaint()
    onStrokeWidthChanged: canvas.requestPaint()
    onTrackColorChanged: canvas.requestPaint()
    onProgressColorChanged: canvas.requestPaint()
    Component.onCompleted: canvas.requestPaint()
}

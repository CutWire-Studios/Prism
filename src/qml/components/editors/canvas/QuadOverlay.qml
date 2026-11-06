import QtQuick
import QtQuick.Shapes
import Prism

// Four draggable corners. corners is [[x, y] x4] normalized to the frame and may leave [0, 1];
// they stay within this item. cornersEdited carries the whole new array.
Item {
    id: root

    property rect frame: Qt.rect(0, 0, 0, 0)
    property var corners: [[0, 0], [1, 0], [1, 1], [0, 1]]

    signal cornersEdited(var corners)

    readonly property real _r: 7
    property int _drag: -1

    function _toPoint(c) {
        return Qt.point(frame.x + c[0] * frame.width, frame.y + c[1] * frame.height)
    }

    function _points() {
        const pts = corners.map(c => _toPoint(c))
        pts.push(pts[0])
        return pts
    }

    function _set(index, px, py) {
        if (frame.width < 1 || frame.height < 1)
            return
        const x = Math.min(width - _r, Math.max(_r, px))
        const y = Math.min(height - _r, Math.max(_r, py))
        const next = corners.map(c => [c[0], c[1]])
        next[index] = [(x - frame.x) / frame.width, (y - frame.y) / frame.height]
        cornersEdited(next)
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.primary
            strokeWidth: 2
            fillColor: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.16)
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root._points() }
        }
    }

    Repeater {
        model: root.corners
        Rectangle {
            required property var modelData
            width: root._r * 2
            height: root._r * 2
            radius: root._r
            color: Theme.primary
            x: root._toPoint(modelData).x - root._r
            y: root._toPoint(modelData).y - root._r
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton

        onPressed: (mouse) => {
            root._drag = -1
            let best = root._r * 2.5
            for (let i = 0; i < root.corners.length; ++i) {
                const p = root._toPoint(root.corners[i])
                const d = Math.hypot(mouse.x - p.x, mouse.y - p.y)
                if (d < best) {
                    best = d
                    root._drag = i
                }
            }
            if (root._drag >= 0)
                root._set(root._drag, mouse.x, mouse.y)
        }
        onPositionChanged: (mouse) => {
            if (pressed && root._drag >= 0)
                root._set(root._drag, mouse.x, mouse.y)
        }
        onReleased: root._drag = -1
    }
}

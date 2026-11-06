import QtQuick
import QtQuick.Shapes
import Prism

// Click to add a vertex, drag to move one, right-click to remove one. points is [[x, y], ...]
// normalized to the frame.
Item {
    id: root

    property rect frame: Qt.rect(0, 0, 0, 0)
    property var points: []

    signal pointsEdited(var points)

    readonly property real _r: 6
    property int _drag: -1

    function _toPoint(c) {
        return Qt.point(frame.x + c[0] * frame.width, frame.y + c[1] * frame.height)
    }

    function _norm(px, py) {
        return [Math.min(1, Math.max(0, (px - frame.x) / frame.width)),
                Math.min(1, Math.max(0, (py - frame.y) / frame.height))]
    }

    function _vertexAt(px, py) {
        for (let i = 0; i < points.length; ++i) {
            const p = _toPoint(points[i])
            if (Math.hypot(px - p.x, py - p.y) < _r * 2)
                return i
        }
        return -1
    }

    function _outline() {
        const pts = points.map(c => _toPoint(c))
        if (pts.length >= 3)
            pts.push(pts[0])
        return pts
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.primary
            strokeWidth: 2
            fillColor: root.points.length >= 3
                       ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.16) : "transparent"
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root._outline() }
        }
    }

    Repeater {
        model: root.points
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

    ThemedLabel {
        visible: root.points.length === 0
        x: root.frame.x + 6
        y: root.frame.y + 6
        width: root.frame.width - 12
        horizontalAlignment: Text.AlignHCenter
        tone: "default"
        color: Theme.onMedia
        text: qsTr("Click to add points. Right-click a point to remove it.")
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onPressed: (mouse) => {
            if (root.frame.width < 1 || root.frame.height < 1)
                return
            const hit = root._vertexAt(mouse.x, mouse.y)
            if (mouse.button === Qt.RightButton) {
                if (hit >= 0) {
                    const next = root.points.slice()
                    next.splice(hit, 1)
                    root.pointsEdited(next)
                }
                return
            }
            if (hit >= 0) {
                root._drag = hit
            } else {
                const next = root.points.slice()
                next.push(root._norm(mouse.x, mouse.y))
                root.pointsEdited(next)
                root._drag = next.length - 1
            }
        }
        onPositionChanged: (mouse) => {
            if (!pressed || root._drag < 0 || root._drag >= root.points.length)
                return
            const next = root.points.slice()
            next[root._drag] = root._norm(mouse.x, mouse.y)
            root.pointsEdited(next)
        }
        onReleased: root._drag = -1
    }
}

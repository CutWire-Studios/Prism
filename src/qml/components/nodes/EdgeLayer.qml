pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Prism

// Every edge as a cubic bezier, plus the temporary wire while a connection is being dragged.
// Endpoints bind to the node delegates' x/y and port lists, so moving a node re-routes its wires
// without any polling. Edges are hit-tested by sampling the curves (edgeAt), not by item events,
// which keeps this layer out of the way of the nodes above it.
Item {
    id: root

    required property var canvas
    readonly property var controller: canvas.controller
    readonly property int count: repeater.count

    // Sample the curve at this many points when hit-testing.
    readonly property int samples: 24

    function _sample(e, t) {
        const u = 1 - t
        return Qt.point(u * u * u * e.p1x + 3 * u * u * t * e.c1x + 3 * u * t * t * e.c2x + t * t * t * e.p2x,
                        u * u * u * e.p1y + 3 * u * u * t * e.p1y + 3 * u * t * t * e.p2y + t * t * t * e.p2y)
    }

    function _segDist2(px, py, ax, ay, bx, by) {
        const dx = bx - ax, dy = by - ay
        const len2 = dx * dx + dy * dy
        let t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0
        t = Math.max(0, Math.min(1, t))
        const cx = ax + t * dx - px, cy = ay + t * dy - py
        return cx * cx + cy * cy
    }

    // Id of the edge nearest to (x, y) in world coordinates within tol, or 0.
    function edgeAt(x, y, tol) {
        let best = 0
        let bestD = tol * tol
        for (let i = 0; i < repeater.count; ++i) {
            const e = repeater.itemAt(i)
            if (!e || !e.valid)
                continue
            const minX = Math.min(e.p1x, e.p2x, e.c1x, e.c2x) - tol
            const maxX = Math.max(e.p1x, e.p2x, e.c1x, e.c2x) + tol
            const minY = Math.min(e.p1y, e.p2y) - tol
            const maxY = Math.max(e.p1y, e.p2y) + tol
            if (x < minX || x > maxX || y < minY || y > maxY)
                continue
            let prev = _sample(e, 0)
            for (let s = 1; s <= samples; ++s) {
                const cur = _sample(e, s / samples)
                const d = _segDist2(x, y, prev.x, prev.y, cur.x, cur.y)
                if (d < bestD) {
                    bestD = d
                    best = e.edgeId
                }
                prev = cur
            }
        }
        return best
    }

    // Id of an edge of the given kind whose curve passes through the rectangle, or 0.
    function edgeUnderRect(rx, ry, rw, rh, kind) {
        for (let i = 0; i < repeater.count; ++i) {
            const e = repeater.itemAt(i)
            if (!e || !e.valid || e.edgeKind !== kind)
                continue
            for (let s = 0; s <= samples; ++s) {
                const p = _sample(e, s / samples)
                if (p.x >= rx && p.x <= rx + rw && p.y >= ry && p.y <= ry + rh)
                    return e.edgeId
            }
        }
        return 0
    }

    Repeater {
        id: repeater
        model: root.controller ? root.controller.edges : null

        delegate: Shape {
            id: edge

            required property var model

            readonly property var edgeId: model.edgeId
            readonly property int edgeKind: model.kind
            readonly property var fromItem: {
                void root.canvas.nodeCount
                void root.canvas.nodesRevision
                return root.canvas.nodeItem(model.fromNode)
            }
            readonly property var toItem: {
                void root.canvas.nodeCount
                void root.canvas.nodesRevision
                return root.canvas.nodeItem(model.toNode)
            }
            readonly property bool valid: fromItem !== null && toItem !== null
            readonly property var _fp: fromItem ? fromItem.outputPorts[model.fromPort] : undefined
            readonly property var _tp: toItem ? toItem.inputPorts[model.toPort] : undefined

            readonly property real p1x: valid ? fromItem.x + (_fp ? _fp.x : fromItem.width) : 0
            readonly property real p1y: valid ? fromItem.y + (_fp ? _fp.y : fromItem.height / 2) : 0
            readonly property real p2x: valid ? toItem.x + (_tp ? _tp.x : 0) : 0
            readonly property real p2y: valid ? toItem.y + (_tp ? _tp.y : toItem.height / 2) : 0
            readonly property real bend: Math.max(Math.abs(p2x - p1x) * 0.4, 40)
            readonly property real c1x: p1x + bend
            readonly property real c2x: p2x - bend

            readonly property bool picked: root.canvas.selectedEdge === edgeId
            readonly property bool hot: root.canvas.hoverEdge === edgeId
            readonly property bool splice: root.canvas.spliceEdgeId === edgeId

            visible: valid
            z: picked ? 2 : 0
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: edge.splice ? Qt.alpha(Theme.foreground, 0.35)
                                         : (edge.picked ? Qt.alpha(Theme.primary, 0.25) : "transparent")
                strokeWidth: 8
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: edge.p1x
                startY: edge.p1y
                PathCubic {
                    x: edge.p2x
                    y: edge.p2y
                    control1X: edge.c1x
                    control1Y: edge.p1y
                    control2X: edge.c2x
                    control2Y: edge.p2y
                }
            }

            ShapePath {
                strokeColor: edge.picked ? Theme.primary : NodePalette.edgeColor(edge.edgeKind)
                strokeWidth: edge.picked ? 3.5 : (edge.hot ? 3 : 2)
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: edge.p1x
                startY: edge.p1y
                PathCubic {
                    x: edge.p2x
                    y: edge.p2y
                    control1X: edge.c1x
                    control1Y: edge.p1y
                    control2X: edge.c2x
                    control2Y: edge.p2y
                }
            }
        }
    }

    // The wire being dragged out of a port.
    Shape {
        id: pending
        visible: !!root.controller && root.controller.connecting
        z: 10
        preferredRendererType: Shape.CurveRenderer

        readonly property real dir: root.canvas.connectIsOutput ? 1 : -1
        readonly property real sx: root.canvas.connectStart.x
        readonly property real sy: root.canvas.connectStart.y
        readonly property real ex: root.canvas.cursorWorld.x
        readonly property real ey: root.canvas.cursorWorld.y
        readonly property real bend: Math.max(Math.abs(ex - sx) * 0.4, 40)

        ShapePath {
            strokeColor: NodePalette.portColor(root.canvas.connectKind)
            strokeWidth: 2
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 3]
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: pending.sx
            startY: pending.sy
            PathCubic {
                x: pending.ex
                y: pending.ey
                control1X: pending.sx + pending.dir * pending.bend
                control1Y: pending.sy
                control2X: pending.ex - pending.dir * pending.bend
                control2Y: pending.ey
            }
        }
    }
}

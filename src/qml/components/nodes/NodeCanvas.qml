pragma ComponentBehavior: Bound
import QtQuick
import Prism

// Pannable, zoomable surface holding the graph. Everything inside `world` is in graph
// coordinates; the grid, rubber band and overlays are in screen coordinates.
//
// Pointer map: wheel zooms about the cursor (trackpad scroll pans, Ctrl+scroll zooms), middle
// drag or Space+drag pans, pinch zooms, left drag on empty space rubber-bands, a drag from a port
// wires a connection, and a click on a wire selects it.
FocusScope {
    id: root

    focus: true
    clip: true

    property var controller: null

    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    readonly property real minZoom: 0.2
    readonly property real maxZoom: 3.0
    // Set once the user pans, zooms or drags a node; until then the view re-fits as the graph loads.
    property bool userMoved: false

    property alias world: worldItem
    readonly property int nodeCount: nodeRepeater.count
    property alias edgeLayer: edges
    property rect contentRect: Qt.rect(0, 0, 0, 0)
    readonly property rect viewRect: Qt.rect(-panX / zoom, -panY / zoom, width / zoom, height / zoom)

    readonly property var selectedMap: {
        const m = {}
        if (controller)
            for (const id of controller.selection)
                m[id] = true
        return m
    }
    property real selectedEdge: 0
    property real hoverEdge: 0
    property real spliceEdgeId: 0

    // Pending connection (the controller owns the real state; this mirrors it for drawing).
    property var connectNode: 0
    property bool connectIsOutput: false
    property int connectPort: -1
    property int connectKind: 0
    property point connectStart: Qt.point(0, 0)
    property point cursorWorld: Qt.point(0, 0)
    property point hoverPos: Qt.point(NaN, NaN)
    property string hoverPortKey: ""
    property bool connectClickMode: false
    property bool spaceDown: false
    property bool spacePanned: false

    // A node was double-clicked: the shell should bring the inspector forward.
    signal inspectorRequested()
    signal outputWindowRequested()
    signal nodeMenuRequested(var item, point worldPos, point screenPos)
    signal edgeMenuRequested(var edgeId, point screenPos)
    signal addMenuRequested(point screenPos, point worldPos)
    // Space was tapped (pressed and released without a pan drag).
    signal quickAddRequested(point screenPos, point worldPos)
    // A connection was dropped on empty space. kind is the PortKind of the dragged port.
    signal connectionDropped(point screenPos, point worldPos, int kind, bool fromOutput)

    // ── Geometry ───────────────────────────────────────────────────────────

    function clampZoom(z) {
        return Math.max(minZoom, Math.min(maxZoom, z))
    }

    function toWorld(sx, sy) {
        return Qt.point((sx - panX) / zoom, (sy - panY) / zoom)
    }

    function toScreen(wx, wy) {
        return Qt.point(wx * zoom + panX, wy * zoom + panY)
    }

    function zoomAt(sx, sy, target) {
        const z = clampZoom(target)
        if (z === zoom)
            return
        userMoved = true
        const wx = (sx - panX) / zoom
        const wy = (sy - panY) / zoom
        zoom = z
        panX = sx - wx * z
        panY = sy - wy * z
    }

    function zoomCentered(factor) {
        zoomAt(width / 2, height / 2, zoom * factor)
    }

    function resetZoom() {
        zoomAt(width / 2, height / 2, 1.0)
    }

    function frameRect(r) {
        if (width <= 0 || height <= 0 || r.width <= 0 || r.height <= 0)
            return
        const margin = 48
        const z = clampZoom(Math.min((width - 2 * margin) / r.width, (height - 2 * margin) / r.height, 1.0))
        zoom = z
        panX = width / 2 - (r.x + r.width / 2) * z
        panY = height / 2 - (r.y + r.height / 2) * z
    }

    function recomputeBounds() {
        let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity
        for (let i = 0; i < nodeRepeater.count; ++i) {
            const it = nodeRepeater.itemAt(i)
            if (!it)
                continue
            x0 = Math.min(x0, it.x)
            y0 = Math.min(y0, it.y)
            x1 = Math.max(x1, it.x + it.width)
            y1 = Math.max(y1, it.y + it.height)
        }
        contentRect = x0 === Infinity ? Qt.rect(0, 0, 0, 0) : Qt.rect(x0, y0, x1 - x0, y1 - y0)
    }

    // Bumped (coalesced) when delegates are created or destroyed, so wires re-resolve their
    // endpoint items even when a model reset leaves the node count unchanged.
    property int nodesRevision: 0

    function noteNodesChanged() {
        Qt.callLater(root._bumpNodes)
    }

    function _bumpNodes() {
        ++nodesRevision
    }

    function scheduleBounds() {
        Qt.callLater(root.recomputeBounds)
    }

    function fitAll() {
        recomputeBounds()
        frameRect(contentRect)
    }

    function frameSelection() {
        const sel = controller ? controller.selection : []
        if (sel.length === 0) {
            fitAll()
            return
        }
        let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity
        for (const id of sel) {
            const it = nodeItem(id)
            if (!it)
                continue
            x0 = Math.min(x0, it.x)
            y0 = Math.min(y0, it.y)
            x1 = Math.max(x1, it.x + it.width)
            y1 = Math.max(y1, it.y + it.height)
        }
        if (x0 !== Infinity)
            frameRect(Qt.rect(x0, y0, x1 - x0, y1 - y0))
    }

    function centerOn(wx, wy) {
        userMoved = true
        panX = width / 2 - wx * zoom
        panY = height / 2 - wy * zoom
    }

    onNodeCountChanged: {
        if (nodeCount <= 1)
            userMoved = false
        if (!userMoved && nodeCount > 1)
            Qt.callLater(root.fitAll)
    }
    onWidthChanged: if (!userMoved && nodeCount > 1) Qt.callLater(root.fitAll)
    onHeightChanged: if (!userMoved && nodeCount > 1) Qt.callLater(root.fitAll)

    // ── Live thumbnails ────────────────────────────────────────────────────
    // Tells the runtime which Input / Process nodes intersect the viewport so only those are
    // re-rendered. Polled (4 Hz) rather than bound, so node drags and pans need no per-change work;
    // a hidden editor reports none, which stops the runtime's thumbnail timer.
    property string _reportedVisible: "-"

    function reportVisibleNodes() {
        const ids = []
        if (root.visible && width > 0 && height > 0) {
            const v = viewRect
            for (let i = 0; i < nodeRepeater.count; ++i) {
                const it = nodeRepeater.itemAt(i)
                if (!it || (it.type !== NodePalette.nodeType.Input && it.type !== NodePalette.nodeType.Process))
                    continue
                if (it.x < v.x + v.width && it.x + it.width > v.x && it.y < v.y + v.height && it.y + it.height > v.y)
                    ids.push(it.nodeId)
            }
        }
        const key = ids.join(",")
        if (key === _reportedVisible)
            return
        _reportedVisible = key
        AppController.setVisibleNodeIds(ids)
    }

    Timer {
        interval: 250
        repeat: true
        running: root.visible
        triggeredOnStart: true
        onTriggered: root.reportVisibleNodes()
    }
    onVisibleChanged: if (!visible) reportVisibleNodes()
    onControllerChanged: _reportedVisible = "-"
    Component.onDestruction: AppController.setVisibleNodeIds([])

    // ── Nodes ──────────────────────────────────────────────────────────────

    function nodeItem(id) {
        if (!controller)
            return null
        const row = controller.nodes.rowOfNode(id)
        if (row < 0)
            return null
        const it = nodeRepeater.itemAt(row)
        return it ? it : null
    }

    function hasNodeOfType(type) {
        for (let i = 0; i < nodeRepeater.count; ++i) {
            const it = nodeRepeater.itemAt(i)
            if (it && it.type === type)
                return true
        }
        return false
    }

    function dragNodes(ids, starts, dx, dy, item) {
        if (ids.length === 1) {
            controller.moveNode(ids[0], starts[0][0] + dx, starts[0][1] + dy)
        } else {
            const coords = []
            for (let i = 0; i < ids.length; ++i)
                coords.push(starts[i][0] + dx, starts[i][1] + dy)
            controller.moveNodes(ids, coords)
        }
        updateSplice(item, ids.length)
    }

    // A lone, unwired node dragged over a wire it can be spliced into highlights it; releasing
    // inserts it. Wired nodes are detached first with Alt.
    function updateSplice(item, dragCount) {
        let id = 0
        if (dragCount === 1 && !isWired(item))
            id = edges.edgeUnderRect(item.x, item.y, item.width, item.height, item.nodeId)
        spliceEdgeId = id
    }

    function isWired(item) {
        for (const p of item.inputPorts)
            if (p.connected)
                return true
        for (const p of item.outputPorts)
            if (p.connected)
                return true
        return false
    }

    function finishNodeDrag(item) {
        controller.endMove()
        if (spliceEdgeId !== 0)
            controller.insertIntoEdge(item.nodeId, spliceEdgeId)
        spliceEdgeId = 0
    }

    function deleteSelection() {
        if (!controller)
            return
        if (selectedEdge !== 0) {
            controller.disconnectEdge(selectedEdge)
            selectedEdge = 0
            return
        }
        const sel = controller.selection
        if (sel.length > 0)
            controller.removeNodes(sel)
    }

    // Adds a node at a world position and, for a pending connection, wires it to the first
    // compatible port of the new node.
    function addNodeAt(type, params, wx, wy, edgeId) {
        const pendingOut = connectIsOutput
        const pending = controller.connecting
        let x = wx
        let y = wy - 28
        if (pending && !pendingOut)
            x = wx - NodePalette.defaultWidth[type]
        const id = edgeId ? controller.addNodeIntoEdge(type, x, y, params || {}, edgeId)
                          : controller.addNodeConnected(type, x, y, params || {})
        if (!id) {
            if (pending)
                controller.cancelConnection()
            return 0
        }
        userMoved = true
        controller.select([id])
        return id
    }

    // ── Connections ────────────────────────────────────────────────────────

    function portCenter(item, isOutput, index) {
        const ports = isOutput ? item.outputPorts : item.inputPorts
        const p = ports[index]
        return p ? Qt.point(item.x + p.x, item.y + p.y) : Qt.point(item.x, item.y)
    }

    // The port within reach of a world position, or null.
    function portAt(wp) {
        const reach = 10
        for (let i = 0; i < nodeRepeater.count; ++i) {
            const it = nodeRepeater.itemAt(i)
            if (!it || wp.x < it.x - reach || wp.x > it.x + it.width + reach
                    || wp.y < it.y - reach || wp.y > it.y + it.height + reach)
                continue
            for (let o = 0; o < 2; ++o) {
                const ports = o === 0 ? it.inputPorts : it.outputPorts
                for (const p of ports) {
                    if (Math.hypot(wp.x - (it.x + p.x), wp.y - (it.y + p.y)) <= reach)
                        return { nodeId: it.nodeId, isOutput: o === 1, index: p.index, kind: p.kind }
                }
            }
        }
        return null
    }

    function _keyOf(hit) {
        return hit ? hit.nodeId + (hit.isOutput ? ":o:" : ":i:") + hit.index : ""
    }

    function resetConnect() {
        connectNode = 0
        connectPort = -1
        connectClickMode = false
        hoverPortKey = ""
        _pressActive = false
    }

    property bool _pressActive: false
    property bool _portMoved: false
    property point _pressWorld: Qt.point(0, 0)

    function portPressed(port, wp) {
        forceActiveFocus()
        selectedEdge = 0
        if (controller.connecting && connectClickMode) {
            if (controller.canConnect(port.node.nodeId, port.isOutput, port.portIndex))
                controller.finishConnection(port.node.nodeId, port.isOutput, port.portIndex)
            else
                controller.cancelConnection()
            resetConnect()
            return
        }
        if (!controller.beginConnection(port.node.nodeId, port.isOutput, port.portIndex))
            return
        connectNode = port.node.nodeId
        connectIsOutput = port.isOutput
        connectPort = port.portIndex
        connectKind = port.kind
        connectStart = portCenter(port.node, port.isOutput, port.portIndex)
        cursorWorld = wp
        _pressWorld = wp
        _portMoved = false
        _pressActive = true
        connectClickMode = false
    }

    function portDragged(wp) {
        if (!_pressActive)
            return
        cursorWorld = wp
        if (!_portMoved && Math.hypot(wp.x - _pressWorld.x, wp.y - _pressWorld.y) * zoom > 4)
            _portMoved = true
        const hit = portAt(wp)
        hoverPortKey = hit && controller.canConnect(hit.nodeId, hit.isOutput, hit.index) ? _keyOf(hit) : ""
    }

    function portReleased(port, wp) {
        if (!_pressActive)
            return
        _pressActive = false
        if (!_portMoved) {
            connectClickMode = true
            return
        }
        const hit = portAt(wp)
        if (hit) {
            if (controller.canConnect(hit.nodeId, hit.isOutput, hit.index))
                controller.finishConnection(hit.nodeId, hit.isOutput, hit.index)
            else
                controller.cancelConnection()
            resetConnect()
        } else {
            hoverPortKey = ""
            const s = toScreen(wp.x, wp.y)
            // The controller keeps the pending connection; the add menu completes or cancels it.
            connectionDropped(s, wp, connectKind, connectIsOutput)
        }
    }

    function portDragCanceled() {
        _pressActive = false
    }

    function cancelConnection() {
        if (controller && controller.connecting)
            controller.cancelConnection()
        resetConnect()
    }

    Connections {
        target: root.controller
        function onConnectingChanged() {
            if (!root.controller.connecting)
                root.resetConnect()
        }
    }

    // ── Input ──────────────────────────────────────────────────────────────

    Keys.onPressed: (event) => {
        if (!controller)
            return
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            spaceDown = true
            spacePanned = false
            event.accepted = true
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            deleteSelection()
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_Z) {
            if (shift)
                controller.redo()
            else
                controller.undo()
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_Y) {
            controller.redo()
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_A) {
            controller.selectAll()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            cancelConnection()
            selectedEdge = 0
            event.accepted = true
        } else if (event.key === Qt.Key_Tab && !ctrl) {
            const at = isNaN(hoverPos.x) ? Qt.point(width / 2, height / 2) : hoverPos
            addMenuRequested(at, toWorld(at.x, at.y))
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_C) {
            controller.copySelection()
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_V) {
            const at = isNaN(hoverPos.x) ? Qt.point(width / 2, height / 2) : hoverPos
            const w = toWorld(at.x, at.y)
            controller.paste(w.x, w.y)
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_D) {
            controller.duplicateSelection()
            event.accepted = true
        } else if (!ctrl && event.key === Qt.Key_F) {
            frameSelection()
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_0) {
            resetZoom()
            event.accepted = true
        } else if (ctrl && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)) {
            zoomCentered(1.12)
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_Minus) {
            zoomCentered(1 / 1.12)
            event.accepted = true
        }
    }

    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            const tapped = spaceDown && !spacePanned
            spaceDown = false
            event.accepted = true
            if (tapped && !controller.connecting) {
                const at = isNaN(hoverPos.x) ? Qt.point(width / 2, height / 2) : hoverPos
                quickAddRequested(at, toWorld(at.x, at.y))
            }
        }
    }

    onActiveFocusChanged: if (!activeFocus) spaceDown = false

    WheelHandler {
        id: wheelHandler
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            const p = wheelHandler.point.position
            const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
            if (!ctrl && (event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0)) {
                root.userMoved = true
                root.panX += event.pixelDelta.x
                root.panY += event.pixelDelta.y
                return
            }
            const dy = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y
            root.zoomAt(p.x, p.y, root.zoom * Math.pow(1.12, dy / 120))
        }
    }

    DragHandler {
        id: middlePan
        target: null
        acceptedButtons: Qt.MiddleButton
        property real startX: 0
        property real startY: 0
        onActiveChanged: if (active) {
            startX = root.panX
            startY = root.panY
            root.userMoved = true
        }
        onTranslationChanged: if (active) {
            root.panX = startX + translation.x
            root.panY = startY + translation.y
        }
    }

    PinchHandler {
        id: pinch
        target: null
        acceptedDevices: PointerDevice.TouchScreen | PointerDevice.TouchPad
        property real startZoom: 1
        property point lastCentroid: Qt.point(0, 0)
        onActiveChanged: if (active) {
            startZoom = root.zoom
            lastCentroid = centroid.position
        }
        onCentroidChanged: if (active) {
            root.panX += centroid.position.x - lastCentroid.x
            root.panY += centroid.position.y - lastCentroid.y
            lastCentroid = centroid.position
        }
        onActiveScaleChanged: if (active)
            root.zoomAt(centroid.position.x, centroid.position.y, startZoom * activeScale)
    }

    HoverHandler {
        onHoveredChanged: if (!hovered) root.hoverPos = Qt.point(NaN, NaN)
        onPointChanged: {
            root.hoverPos = point.position
            if (root.controller && root.controller.connecting && !root._pressActive) {
                root.cursorWorld = root.toWorld(point.position.x, point.position.y)
                const hit = root.portAt(root.cursorWorld)
                root.hoverPortKey = hit && root.controller.canConnect(hit.nodeId, hit.isOutput, hit.index)
                                    ? root._keyOf(hit) : ""
            }
        }
    }

    // ── Scene ──────────────────────────────────────────────────────────────

    NodeGridItem {
        anchors.fill: parent
        zoom: root.zoom
        panX: root.panX
        panY: root.panY
        minorColor: Qt.alpha(Theme.foreground, 0.07)
        majorColor: Qt.alpha(Theme.foreground, 0.14)
    }

    MouseArea {
        id: bgArea
        anchors.fill: parent
        enabled: root.controller !== null
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true

        property point pressPos: Qt.point(0, 0)
        property bool banding: false
        property var baseSelection: []
        property string lastBandKey: ""

        function pickEdge(m) {
            const w = root.toWorld(m.x, m.y)
            return root.edgeLayer.edgeAt(w.x, w.y, 7 / root.zoom)
        }

        onPressed: (m) => {
            root.forceActiveFocus()
            if (root.controller.connecting) {
                root.cancelConnection()
                return
            }
            const e = pickEdge(m)
            if (m.button === Qt.RightButton) {
                if (e !== 0) {
                    root.selectedEdge = e
                    root.edgeMenuRequested(e, Qt.point(m.x, m.y))
                } else {
                    const w = root.toWorld(m.x, m.y)
                    root.addMenuRequested(Qt.point(m.x, m.y), w)
                }
                return
            }
            if (e !== 0) {
                root.selectedEdge = e
                root.controller.clearSelection()
                return
            }
            root.selectedEdge = 0
            const additive = (m.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
            baseSelection = additive ? root.controller.selection.slice() : []
            if (!additive)
                root.controller.clearSelection()
            pressPos = Qt.point(m.x, m.y)
            banding = false
            lastBandKey = ""
        }

        onPositionChanged: (m) => {
            if (!pressed) {
                if (root.edgeLayer.count > 0) {
                    const e = pickEdge(m)
                    if (e !== root.hoverEdge)
                        root.hoverEdge = e
                }
                return
            }
            if (!(pressedButtons & Qt.LeftButton))
                return
            if (!banding && Math.hypot(m.x - pressPos.x, m.y - pressPos.y) > 4)
                banding = true
            if (!banding)
                return
            const a = root.toWorld(Math.min(pressPos.x, m.x), Math.min(pressPos.y, m.y))
            const b = root.toWorld(Math.max(pressPos.x, m.x), Math.max(pressPos.y, m.y))
            const ids = baseSelection.slice()
            for (let i = 0; i < nodeRepeater.count; ++i) {
                const it = nodeRepeater.itemAt(i)
                if (it && it.x < b.x && it.x + it.width > a.x && it.y < b.y && it.y + it.height > a.y
                        && ids.indexOf(it.nodeId) < 0)
                    ids.push(it.nodeId)
            }
            const key = ids.join(",")
            if (key !== lastBandKey) {
                lastBandKey = key
                root.controller.select(ids, false)
            }
            band.x = Math.min(pressPos.x, m.x)
            band.y = Math.min(pressPos.y, m.y)
            band.width = Math.abs(m.x - pressPos.x)
            band.height = Math.abs(m.y - pressPos.y)
        }

        onReleased: banding = false
        onExited: root.hoverEdge = 0

        onDoubleClicked: (m) => {
            if (m.button === Qt.LeftButton && pickEdge(m) === 0)
                root.addMenuRequested(Qt.point(m.x, m.y), root.toWorld(m.x, m.y))
        }
    }

    Item {
        id: worldItem
        x: root.panX
        y: root.panY
        scale: root.zoom
        transformOrigin: Item.TopLeft

        EdgeLayer {
            id: edges
            canvas: root
        }

        Repeater {
            id: nodeRepeater
            model: root.controller ? root.controller.nodes : null
            delegate: NodeDelegate {
                canvas: root
            }
        }
    }

    Rectangle {
        id: band
        visible: bgArea.banding && bgArea.pressed
        color: Qt.alpha(Theme.primary, 0.12)
        border.width: 1
        border.color: Theme.primary
    }

    MouseArea {
        id: spacePan
        anchors.fill: parent
        enabled: root.spaceDown
        visible: root.spaceDown
        z: 100
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real startX: 0
        property real startY: 0
        property point pressPoint: Qt.point(0, 0)
        onPressed: (m) => {
            startX = root.panX
            startY = root.panY
            pressPoint = Qt.point(m.x, m.y)
            root.spacePanned = true
            root.userMoved = true
        }
        onPositionChanged: (m) => {
            if (!pressed)
                return
            root.panX = startX + m.x - pressPoint.x
            root.panY = startY + m.y - pressPoint.y
        }
    }
}

pragma ComponentBehavior: Bound
import QtQuick
import Prism

// Layer placement canvas. slots is topmost first: [{slot, name, visible, sourceNode, x, y, w, h}]
// with rects normalized to the canvas. Moves, resizes and nudges emit rectEdited; the owner
// writes them back into slots.
Item {
    id: root

    property int canvasW: 1920
    property int canvasH: 1080
    property var slots: []
    property int selectedSlot: -1

    signal rectEdited(int slot, real x, real y, real w, real h)
    signal slotSelected(int slot)

    readonly property real _margin: 16
    readonly property real _minSize: 0.02
    readonly property real _snapPx: 6

    readonly property real _scale: Math.min((width - 2 * _margin) / Math.max(1, canvasW),
                                            (height - 2 * _margin) / Math.max(1, canvasH))
    readonly property rect canvasRect: Qt.rect((width - canvasW * _scale) / 2,
                                               (height - canvasH * _scale) / 2,
                                               canvasW * _scale, canvasH * _scale)

    property var _guidesX: []
    property var _guidesY: []
    property int _dragSlot: -1
    property string _handle: ""
    property point _press: Qt.point(0, 0)
    property var _orig: null
    property string _readout: ""

    clip: true
    focus: true
    activeFocusOnTab: true

    function _find(slot) {
        for (let i = 0; i < slots.length; ++i)
            if (slots[i].slot === slot)
                return slots[i]
        return null
    }

    function _screen(s) {
        const c = canvasRect
        return Qt.rect(c.x + s.x * c.width, c.y + s.y * c.height, s.w * c.width, s.h * c.height)
    }

    function _handlePoints(r) {
        const cx = r.x + r.width / 2, cy = r.y + r.height / 2
        const x2 = r.x + r.width, y2 = r.y + r.height
        return [["nw", r.x, r.y], ["n", cx, r.y], ["ne", x2, r.y], ["e", x2, cy],
                ["se", x2, y2], ["s", cx, y2], ["sw", r.x, y2], ["w", r.x, cy]]
    }

    function _slotAt(px, py) {
        for (const s of slots) {
            const r = _screen(s)
            if (px >= r.x && px <= r.x + r.width && py >= r.y && py <= r.y + r.height)
                return s.slot
        }
        return -1
    }

    function _handleAt(px, py) {
        const sel = _find(selectedSlot)
        if (sel) {
            for (const h of _handlePoints(_screen(sel)))
                if (Math.abs(px - h[1]) <= 7 && Math.abs(py - h[2]) <= 7)
                    return { slot: sel.slot, handle: h[0] }
        }
        const hit = _slotAt(px, py)
        return { slot: hit, handle: hit >= 0 ? "move" : "" }
    }

    function _targets() {
        const xs = [0, 0.5, 1], ys = [0, 0.5, 1]
        for (const s of slots) {
            if (s.slot === _dragSlot || !s.visible)
                continue
            xs.push(s.x, s.x + s.w / 2, s.x + s.w)
            ys.push(s.y, s.y + s.h / 2, s.y + s.h)
        }
        return { xs: xs, ys: ys }
    }

    function _snapMove(r) {
        const c = canvasRect
        const thX = _snapPx / c.width, thY = _snapPx / c.height
        const t = _targets()
        let dx = 0, dy = 0, bx = thX, by = thY
        let gx = [], gy = []
        const candX = [r.x, r.x + r.w / 2, r.x + r.w]
        const candY = [r.y, r.y + r.h / 2, r.y + r.h]
        for (const tx of t.xs) for (const cv of candX)
            if (Math.abs(tx - cv) < bx) { bx = Math.abs(tx - cv); dx = tx - cv; gx = [tx] }
        for (const ty of t.ys) for (const cv of candY)
            if (Math.abs(ty - cv) < by) { by = Math.abs(ty - cv); dy = ty - cv; gy = [ty] }
        _guidesX = gx
        _guidesY = gy
        return { x: r.x + dx, y: r.y + dy, w: r.w, h: r.h }
    }

    function _snapEdge(v, targets, th, guides) {
        let best = v, bestA = th
        for (const t of targets)
            if (Math.abs(t - v) < bestA) { bestA = Math.abs(t - v); best = t }
        if (best !== v)
            guides.push(best)
        return best
    }

    function _clampMove(r) {
        return { x: Math.min(Math.max(0, r.x), Math.max(0, 1 - r.w)),
                 y: Math.min(Math.max(0, r.y), Math.max(0, 1 - r.h)), w: r.w, h: r.h }
    }

    function _apply(slot, r) {
        _readout = Math.round(r.x * canvasW) + ", " + Math.round(r.y * canvasH) + "    "
                   + Math.round(r.w * canvasW) + " × " + Math.round(r.h * canvasH)
        rectEdited(slot, r.x, r.y, r.w, r.h)
    }

    function _drag(mouse) {
        const c = canvasRect
        let dx = (mouse.x - _press.x) / c.width
        let dy = (mouse.y - _press.y) / c.height
        const shift = (mouse.modifiers & Qt.ShiftModifier) !== 0
        const noSnap = (mouse.modifiers & Qt.AltModifier) !== 0
        const o = _orig
        _guidesX = []
        _guidesY = []
        let r = { x: o.x, y: o.y, w: o.w, h: o.h }

        if (_handle === "move") {
            if (shift) {
                if (Math.abs(dx * c.width) >= Math.abs(dy * c.height)) dy = 0
                else dx = 0
            }
            r.x += dx
            r.y += dy
            if (!noSnap) r = _snapMove(r)
            r = _clampMove(r)
        } else {
            const left = _handle.indexOf("w") >= 0
            const right = _handle.indexOf("e") >= 0
            const top = _handle.indexOf("n") >= 0
            const bottom = _handle.indexOf("s") >= 0
            let l = o.x, t = o.y, rr = o.x + o.w, b = o.y + o.h
            if (left) l += dx
            if (right) rr += dx
            if (top) t += dy
            if (bottom) b += dy
            if (!noSnap) {
                const tg = _targets()
                const th = { x: _snapPx / c.width, y: _snapPx / c.height }
                const gx = [], gy = []
                if (left) l = _snapEdge(l, tg.xs, th.x, gx)
                if (right) rr = _snapEdge(rr, tg.xs, th.x, gx)
                if (top) t = _snapEdge(t, tg.ys, th.y, gy)
                if (bottom) b = _snapEdge(b, tg.ys, th.y, gy)
                _guidesX = gx
                _guidesY = gy
            }
            const corner = (left || right) && (top || bottom)
            if (shift && corner && o.h > 1e-6) {
                const ar = o.w / o.h
                let w = Math.abs(rr - l), h = Math.abs(b - t)
                if (w / ar > h) w = h * ar
                else h = w / ar
                l = left ? o.x + o.w - w : o.x
                t = top ? o.y + o.h - h : o.y
                rr = l + w
                b = t + h
                _guidesX = []
                _guidesY = []
            }
            if (rr < l) { const s = l; l = rr; rr = s }
            if (b < t) { const s = t; t = b; b = s }
            l = Math.max(0, l); t = Math.max(0, t); rr = Math.min(1, rr); b = Math.min(1, b)
            if (rr - l < _minSize) { if (left) l = rr - _minSize; else rr = l + _minSize }
            if (b - t < _minSize) { if (top) t = b - _minSize; else b = t + _minSize }
            r = { x: Math.max(0, l), y: Math.max(0, t), w: Math.min(1, rr) - Math.max(0, l), h: Math.min(1, b) - Math.max(0, t) }
        }
        _apply(_dragSlot, r)
    }

    Keys.onPressed: (event) => {
        const s = _find(selectedSlot)
        if (event.key === Qt.Key_Escape && s) {
            slotSelected(-1)
            event.accepted = true
            return
        }
        if (!s)
            return
        const step = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
        let dx = 0, dy = 0
        if (event.key === Qt.Key_Left) dx = -step / canvasW
        else if (event.key === Qt.Key_Right) dx = step / canvasW
        else if (event.key === Qt.Key_Up) dy = -step / canvasH
        else if (event.key === Qt.Key_Down) dy = step / canvasH
        else return
        const r = _clampMove({ x: s.x + dx, y: s.y + dy, w: s.w, h: s.h })
        rectEdited(s.slot, r.x, r.y, r.w, r.h)
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.previewLetterbox
    }

    Rectangle {
        x: root.canvasRect.x
        y: root.canvasRect.y
        width: root.canvasRect.width
        height: root.canvasRect.height
        color: Theme.appBackground
        border.width: Theme.borderWidth
        border.color: Theme.border
    }

    Item {
        x: root.canvasRect.x
        y: root.canvasRect.y
        width: root.canvasRect.width
        height: root.canvasRect.height

        Repeater {
            model: root.slots.slice().reverse()

            Item {
                id: tile
                required property var modelData
                readonly property rect r: root._screen(modelData)
                readonly property bool selected: modelData.slot === root.selectedSlot
                x: r.x - root.canvasRect.x
                y: r.y - root.canvasRect.y
                width: r.width
                height: r.height

                Image {
                    anchors.fill: parent
                    source: tile.modelData.sourceNode > 0 ? "image://nodethumb/" + tile.modelData.sourceNode : ""
                    fillMode: Image.Stretch
                    opacity: tile.modelData.visible ? 1 : 0.25
                }

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: tile.selected ? 2 : 1
                    border.color: tile.selected ? Theme.primary : Theme.guideStrong
                }

                Rectangle {
                    visible: tile.width > 70 && tile.height > 30
                    x: 4
                    y: 4
                    width: Math.min(chipText.implicitWidth + 12, tile.width - 8)
                    height: 17
                    radius: 3
                    color: Theme.overlayColor

                    ThemedLabel {
                        id: chipText
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 4
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                        tone: "default"
                        color: tile.selected ? Theme.primary : Theme.onMedia
                        text: tile.modelData.name + (tile.modelData.visible ? "" : "  ·  " + qsTr("hidden"))
                    }
                }
            }
        }
    }

    Repeater {
        model: root._guidesX
        Rectangle {
            required property real modelData
            x: root.canvasRect.x + modelData * root.canvasRect.width
            y: root.canvasRect.y
            width: 1
            height: root.canvasRect.height
            color: Theme.snapGuide
        }
    }

    Repeater {
        model: root._guidesY
        Rectangle {
            required property real modelData
            x: root.canvasRect.x
            y: root.canvasRect.y + modelData * root.canvasRect.height
            width: root.canvasRect.width
            height: 1
            color: Theme.snapGuide
        }
    }

    Repeater {
        model: {
            const s = root._find(root.selectedSlot)
            return s ? root._handlePoints(root._screen(s)) : []
        }
        Rectangle {
            required property var modelData
            width: 8
            height: 8
            x: modelData[1] - 4
            y: modelData[2] - 4
            color: Theme.primary
            border.width: 1
            border.color: Theme.appBackground
        }
    }

    Rectangle {
        visible: root._dragSlot >= 0
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.canvasRect.y + root.canvasRect.height - height - 6
        width: readoutText.implicitWidth + 20
        height: 20
        radius: 4
        color: Theme.overlayColor

        ThemedLabel {
            id: readoutText
            anchors.centerIn: parent
            wrapMode: Text.NoWrap
            tone: "default"
            color: Theme.onMedia
            text: root._readout
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: {
            const h = root._handleAt(mouseX, mouseY).handle
            return (h === "nw" || h === "se") ? Qt.SizeFDiagCursor
                 : (h === "ne" || h === "sw") ? Qt.SizeBDiagCursor
                 : (h === "n" || h === "s") ? Qt.SizeVerCursor
                 : (h === "e" || h === "w") ? Qt.SizeHorCursor
                 : h === "move" ? Qt.SizeAllCursor : Qt.ArrowCursor
        }

        onPressed: (mouse) => {
            root.forceActiveFocus()
            const hit = root._handleAt(mouse.x, mouse.y)
            if (hit.handle === "") {
                root.slotSelected(-1)
                return
            }
            if (hit.handle === "move")
                root.slotSelected(hit.slot)
            const s = root._find(hit.slot)
            root._dragSlot = hit.slot
            root._handle = hit.handle
            root._press = Qt.point(mouse.x, mouse.y)
            root._orig = { x: s.x, y: s.y, w: s.w, h: s.h }
        }
        onPositionChanged: (mouse) => {
            if (pressed && root._dragSlot >= 0)
                root._drag(mouse)
        }
        onReleased: {
            root._dragSlot = -1
            root._handle = ""
            root._guidesX = []
            root._guidesY = []
        }
        onDoubleClicked: (mouse) => {
            const slot = root._slotAt(mouse.x, mouse.y)
            if (slot < 0)
                return
            root.slotSelected(slot)
            root.rectEdited(slot, 0, 0, 1, 1)
        }
    }
}

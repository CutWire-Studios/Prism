import QtQuick
import Prism

// Crop rectangle over a frame. cx..ch are normalized to the frame; every drag emits cropEdited
// and the owner writes the values back.
Item {
    id: root

    property rect frame: Qt.rect(0, 0, 0, 0)
    property real cx: 0
    property real cy: 0
    property real cw: 1
    property real ch: 1

    signal cropEdited(real x, real y, real w, real h)

    readonly property real _l: frame.x + cx * frame.width
    readonly property real _t: frame.y + cy * frame.height
    readonly property real _r: _l + cw * frame.width
    readonly property real _b: _t + ch * frame.height

    property string _mode: ""
    property point _origin: Qt.point(0, 0)
    property rect _start: Qt.rect(0, 0, 0, 0)
    property bool _moved: false

    function _clamp(v, lo, hi) {
        return hi < lo ? lo : Math.min(hi, Math.max(lo, v))
    }

    function _hit(px, py) {
        const reach = 8
        const corners = [[_l, _t, "tl"], [_r, _t, "tr"], [_r, _b, "br"], [_l, _b, "bl"]]
        for (const c of corners) {
            if (Math.abs(px - c[0]) <= reach && Math.abs(py - c[1]) <= reach)
                return c[2]
        }
        return (px >= _l && px <= _r && py >= _t && py <= _b) ? "move" : ""
    }

    function _emit(l, t, r, b) {
        if (frame.width < 1 || frame.height < 1)
            return
        cropEdited((l - frame.x) / frame.width, (t - frame.y) / frame.height,
                   (r - l) / frame.width, (b - t) / frame.height)
    }

    Rectangle { x: root.frame.x; y: root.frame.y; width: root.frame.width; height: Math.max(0, root._t - root.frame.y); color: Theme.scrimColor }
    Rectangle { x: root.frame.x; y: root._b; width: root.frame.width; height: Math.max(0, root.frame.y + root.frame.height - root._b); color: Theme.scrimColor }
    Rectangle { x: root.frame.x; y: root._t; width: Math.max(0, root._l - root.frame.x); height: root._b - root._t; color: Theme.scrimColor }
    Rectangle { x: root._r; y: root._t; width: Math.max(0, root.frame.x + root.frame.width - root._r); height: root._b - root._t; color: Theme.scrimColor }

    Rectangle {
        x: root._l
        y: root._t
        width: root._r - root._l
        height: root._b - root._t
        color: "transparent"
        border.width: 2
        border.color: Theme.primary
    }

    Repeater {
        model: [[0, 0], [1, 0], [1, 1], [0, 1]]
        Rectangle {
            id: handle
            required property var modelData
            width: 9
            height: 9
            x: (modelData[0] ? root._r : root._l) - width / 2
            y: (modelData[1] ? root._b : root._t) - height / 2
            color: Theme.primary
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: {
            const m = root._hit(mouseX, mouseY)
            return m === "move" ? Qt.SizeAllCursor
                 : (m === "tl" || m === "br") ? Qt.SizeFDiagCursor
                 : (m === "tr" || m === "bl") ? Qt.SizeBDiagCursor : Qt.CrossCursor
        }

        onPressed: (mouse) => {
            const hit = root._hit(mouse.x, mouse.y)
            root._mode = hit.length > 0 ? hit : "create"
            root._origin = Qt.point(mouse.x, mouse.y)
            root._start = hit.length > 0 ? Qt.rect(root._l, root._t, root._r - root._l, root._b - root._t)
                                         : Qt.rect(mouse.x, mouse.y, 0, 0)
            root._moved = false
        }

        onPositionChanged: (mouse) => {
            if (!pressed || root._mode === "")
                return
            const f = root.frame
            if (f.width < 1 || f.height < 1)
                return
            const dx = mouse.x - root._origin.x
            const dy = mouse.y - root._origin.y
            if (Math.abs(dx) >= 1 || Math.abs(dy) >= 1)
                root._moved = true
            const s = root._start
            const sl = s.x, st = s.y, sr = s.x + s.width, sb = s.y + s.height
            const minW = f.width * 0.02
            const minH = f.height * 0.02
            const fl = f.x, ft = f.y, fr = f.x + f.width, fb = f.y + f.height
            let l = sl, t = st, r = sr, b = sb

            switch (root._mode) {
            case "create":
                l = root._clamp(Math.min(root._origin.x, mouse.x), fl, fr)
                t = root._clamp(Math.min(root._origin.y, mouse.y), ft, fb)
                r = root._clamp(Math.max(root._origin.x, mouse.x), fl, fr)
                b = root._clamp(Math.max(root._origin.y, mouse.y), ft, fb)
                break
            case "move": {
                const w = sr - sl, h = sb - st
                l = root._clamp(sl + dx, fl, fr - w)
                t = root._clamp(st + dy, ft, fb - h)
                r = l + w
                b = t + h
                break
            }
            case "tl":
                l = root._clamp(sl + dx, fl, sr - minW)
                t = root._clamp(st + dy, ft, sb - minH)
                break
            case "tr":
                r = root._clamp(sr + dx, sl + minW, fr)
                t = root._clamp(st + dy, ft, sb - minH)
                break
            case "br":
                r = root._clamp(sr + dx, sl + minW, fr)
                b = root._clamp(sb + dy, st + minH, fb)
                break
            case "bl":
                l = root._clamp(sl + dx, fl, sr - minW)
                b = root._clamp(sb + dy, st + minH, fb)
                break
            }
            root._emit(l, t, r, b)
        }

        onReleased: {
            if (root._mode === "create" && !root._moved)
                root.cropEdited(0, 0, 1, 1)
            root._mode = ""
        }

        onDoubleClicked: root.cropEdited(0, 0, 1, 1)
    }
}

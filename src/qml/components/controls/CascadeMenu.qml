pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Prism

// Cascading menu built from a tree of plain entries:
//   { text, icon (Lucide name), hint, enabled, header, separator, children: [entries], action: fn }
//
// Qt's nested Menus place submenus unreliably with Popup.Window and switch submenus as soon as the
// pointer crosses a sibling row. Here every column lives in one window-sized popup, so a submenu
// always opens beside the row that opened it (flipping left at the window edge), and a pointer
// heading for an open submenu (inside the triangle from the pointer to the submenu's near edge)
// does not switch it until it rests there.
Popup {
    id: root

    property var model: []
    property bool searchable: false
    property string searchPlaceholder: qsTr("Search…")
    // (query) => flat array of entries shown in the first column while the query is non-empty.
    property var searchFn: null
    property real minColumnWidth: 160
    property real searchColumnWidth: 280
    // Set when an entry was picked; reset on every open.
    property bool triggered: false

    readonly property int maxDepth: 6
    readonly property real edgeMargin: Theme.spacingSm
    readonly property real pad: Theme.spacingSm
    readonly property real gap: 2
    readonly property real itemHeight: Theme.controlHeightSm + Theme.spacingSm
    readonly property real headerHeight: Theme.controlHeightSm
    readonly property real separatorHeight: 2 * Theme.spacingXs + Theme.c.themedMenuSeparator.borderWidth

    property int activeLevel: 0
    property point origin: Qt.point(0, 0)
    property real rootWidth: minColumnWidth
    property var history: []
    property var pending: null
    property string query: ""
    property Item searchField: null
    property int cursor: Qt.ArrowCursor

    function popupAt(item, px, py) {
        triggered = false
        origin = item.mapToItem(root.parent, px, py)
        pending = null
        safeTimer.stop()
        history = []
        query = ""
        if (searchField)
            searchField.text = ""
        rootWidth = Math.max(layout(model, minColumnWidth).width, searchable ? searchColumnWidth : 0)
        truncate(0)
        open()
        showRoot()
        Haptics.press()
    }

    function column(level) {
        return level >= 0 && level < maxDepth ? columns.itemAt(level) : null
    }

    function selectable(e) {
        return !!e && !e.separator && !e.header && e.enabled !== false
    }

    function hasChildren(e) {
        return !!e && !!e.children && e.children.length > 0
    }

    function layout(entries, minWidth) {
        const out = []
        let y = 0
        let textW = 0
        let hintW = 0
        let icons = false
        let arrows = false
        for (const e of entries) {
            if (e.separator && (out.length === 0 || out[out.length - 1].separator))
                continue
            const h = e.separator ? separatorHeight : e.header ? headerHeight : itemHeight
            out.push(Object.assign({}, e, { _y: y, _h: h }))
            y += h
            if (e.separator)
                continue
            textW = Math.max(textW, metrics.advanceWidth(e.text || "") * (e.header ? 1.1 : 1))
            if (e.hint)
                hintW = Math.max(hintW, metrics.advanceWidth(e.hint))
            icons = icons || !!e.icon
            arrows = arrows || hasChildren(e)
        }
        while (out.length > 0 && out[out.length - 1].separator)
            y -= out.pop()._h
        const slot = Theme.iconSizeMd + Theme.spacingLg
        const w = 2 * pad + 2 * Theme.spacingLg + (icons ? slot : 0) + textW
                  + (hintW > 0 ? 2 * Theme.spacingLg + hintW : 0) + (arrows ? slot : 0)
        return { entries: out, height: y, width: Math.ceil(Math.max(minWidth, w)), icons: icons }
    }

    function fill(level, laid, width) {
        const c = column(level)
        c.entries = laid.entries
        c.contentH = laid.height
        c.colW = width
        c.hasIcons = laid.icons
        c.hover = -1
        c.openIndex = -1
        c.contentY = 0
        c.shown = true
        return c
    }

    // Hides `level` and everything deeper.
    function truncate(level) {
        for (let l = level; l < maxDepth; ++l) {
            const c = column(l)
            if (!c.shown)
                break
            c.shown = false
            c.hover = -1
            c.openIndex = -1
            c.entries = []
        }
        if (level > 0)
            column(level - 1).openIndex = -1
        if (activeLevel >= level)
            activeLevel = Math.max(0, level - 1)
    }

    function showRoot() {
        const q = query.trim()
        const searching = searchable && q.length > 0
        const entries = searching ? (searchFn ? searchFn(q) : []) : model
        truncate(1)
        const c = fill(0, layout(entries, rootWidth), rootWidth)
        const m = edgeMargin
        let x = origin.x
        let y = origin.y
        c.flipped = false
        if (x + c.width > width - m) {
            c.flipped = origin.x - c.width >= m
            x = c.flipped ? origin.x - c.width : Math.max(m, width - m - c.width)
        }
        if (y + c.height > height - m)
            y = Math.max(m, height - m - c.height)
        c.x = x
        c.y = y
        activeLevel = 0
        if (searching)
            c.hover = nextSelectable(c, -1, 1)
    }

    function openSub(level, index) {
        const c = column(level)
        const e = c.entries[index]
        truncate(level + 1)
        c.openIndex = index
        const laid = layout(e.children, minColumnWidth)
        const s = fill(level + 1, laid, laid.width)
        const m = edgeMargin
        const right = c.x + c.width + gap
        const left = c.x - s.width - gap
        const fitsRight = right + s.width <= width - m
        if (fitsRight && !c.flipped) {
            s.x = right
            s.flipped = false
        } else if (left >= m) {
            s.x = left
            s.flipped = true
        } else {
            s.x = fitsRight ? right : Math.max(m, width - m - s.width)
            s.flipped = false
        }
        const rowTop = c.y + pad + c.headerH + e._y - c.contentY
        s.y = Math.max(m, Math.min(rowTop - pad, height - m - s.height))
    }

    function activate(level, index) {
        safeTimer.stop()
        pending = null
        const c = column(level)
        activeLevel = level
        c.hover = index
        if (index < 0 || c.openIndex === index)
            return
        const e = c.entries[index]
        if (selectable(e) && hasChildren(e))
            openSub(level, index)
        else
            truncate(level + 1)
    }

    function trigger(level, index) {
        const e = column(level).entries[index]
        if (!selectable(e))
            return
        if (hasChildren(e)) {
            activate(level, index)
            return
        }
        triggered = true
        // closed() fires synchronously (no exit transition), so a consumer's onClosed focus reset
        // runs before the action and does not steal focus from an editor the action opens.
        close()
        Haptics.press()
        if (e.action)
            e.action()
    }

    function hitTest(px, py) {
        for (let l = maxDepth - 1; l >= 0; --l) {
            const c = column(l)
            if (!c.shown || px < c.x || px >= c.x + c.width || py < c.y || py >= c.y + c.height)
                continue
            const ly = py - c.y
            const inHeader = c.headerH > 0 && ly >= pad && ly < pad + c.headerH
            let index = -1
            if (ly >= pad + c.headerH && ly < c.height - pad) {
                const cy = ly - pad - c.headerH + c.contentY
                for (let i = 0; i < c.entries.length; ++i) {
                    const e = c.entries[i]
                    if (cy >= e._y && cy < e._y + e._h) {
                        index = i
                        break
                    }
                }
            }
            return { level: l, index: index, inHeader: inHeader }
        }
        return null
    }

    function cross(a, b, p) {
        return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)
    }

    // Whether the move from `prev` to `cur` heads into submenu column `s`.
    function headingInto(prev, cur, s) {
        if (!prev || (prev.x === cur.x && prev.y === cur.y))
            return false
        const ex = s.flipped ? s.x + s.width : s.x
        const tol = Theme.spacingLg
        const a = Qt.point(ex, s.y - tol)
        const b = Qt.point(ex, s.y + s.height + tol)
        const d1 = cross(prev, a, cur)
        const d2 = cross(a, b, cur)
        const d3 = cross(b, prev, cur)
        const neg = d1 < 0 || d2 < 0 || d3 < 0
        const pos = d1 > 0 || d2 > 0 || d3 > 0
        return !(neg && pos)
    }

    function pointerMoved(px, py) {
        const cur = Qt.point(px, py)
        // The oldest of the last few samples: single-event deltas are too jittery to give a direction.
        const prev = history.length > 0 ? history[0] : null
        const h = history.slice(-2)
        h.push(cur)
        history = h

        const hit = hitTest(px, py)
        if (!hit) {
            cursor = Qt.ArrowCursor
            return
        }
        const c = column(hit.level)
        cursor = hit.inHeader ? Qt.IBeamCursor
                              : hit.index >= 0 && selectable(c.entries[hit.index]) ? Qt.PointingHandCursor
                                                                                   : Qt.ArrowCursor
        if (hit.index < 0) {
            if (!pending)
                c.hover = -1
            return
        }
        const s = column(hit.level + 1)
        if (s && s.shown && c.openIndex !== hit.index && headingInto(prev, cur, s)) {
            pending = { level: hit.level, index: hit.index }
            safeTimer.restart()
            return
        }
        activate(hit.level, hit.index)
    }

    function nextSelectable(c, from, step) {
        for (let i = from + step; i >= 0 && i < c.entries.length; i += step) {
            if (selectable(c.entries[i]))
                return i
        }
        return -1
    }

    function ensureVisible(c, index) {
        const e = c.entries[index]
        if (e._y < c.contentY)
            c.contentY = e._y
        else if (e._y + e._h > c.contentY + c.viewHeight)
            c.contentY = e._y + e._h - c.viewHeight
    }

    function moveHover(step) {
        const c = column(activeLevel)
        let from = c.hover >= 0 ? c.hover : c.openIndex
        if (from < 0)
            from = step > 0 ? -1 : c.entries.length
        const i = nextSelectable(c, from, step)
        if (i < 0)
            return
        if (c.openIndex >= 0 && c.openIndex !== i)
            truncate(activeLevel + 1)
        c.hover = i
        ensureVisible(c, i)
    }

    function enterSub() {
        const level = activeLevel
        const c = column(level)
        const i = c.hover
        if (i < 0 || !selectable(c.entries[i]) || !hasChildren(c.entries[i]))
            return
        if (c.openIndex !== i)
            openSub(level, i)
        const s = column(level + 1)
        activeLevel = level + 1
        s.hover = nextSelectable(s, -1, 1)
    }

    function back() {
        const parentCol = column(activeLevel - 1)
        const index = parentCol.openIndex
        truncate(activeLevel)
        parentCol.hover = index
    }

    function handleKey(event) {
        const c = column(activeLevel)
        const typing = searchable && query.length > 0
        switch (event.key) {
        case Qt.Key_Down:
            moveHover(1)
            break
        case Qt.Key_Up:
            moveHover(-1)
            break
        case Qt.Key_Right:
            if (typing)
                return
            enterSub()
            break
        case Qt.Key_Left:
            if (typing)
                return
            if (activeLevel > 0)
                back()
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (c.hover >= 0) {
                if (hasChildren(c.entries[c.hover]))
                    enterSub()
                else
                    trigger(activeLevel, c.hover)
            }
            break
        case Qt.Key_Escape:
            if (activeLevel > 0)
                back()
            else
                close()
            break
        default:
            return
        }
        event.accepted = true
    }

    function scrollColumn(level, dy) {
        const c = column(level)
        const y = Math.max(0, Math.min(c.contentY + dy, c.contentH - c.viewHeight))
        if (y === c.contentY)
            return
        c.contentY = y
        truncate(level + 1)
    }

    onQueryChanged: {
        if (visible)
            showRoot()
    }

    onOpened: {
        if (searchField)
            searchField.forceActiveFocus()
        else
            keySink.forceActiveFocus()
    }

    onClosed: {
        safeTimer.stop()
        pending = null
    }

    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    padding: 0
    margins: 0
    modal: false
    dim: false
    focus: true
    popupType: Popup.Item
    closePolicy: Popup.NoAutoClose
    background: null

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0.0
            to: 1.0
            duration: Theme.durationFast
            easing.type: Theme.easing
        }
    }

    Timer {
        id: safeTimer
        interval: 300
        onTriggered: {
            const p = root.pending
            root.pending = null
            const last = root.history[root.history.length - 1]
            const hit = last ? root.hitTest(last.x, last.y) : null
            if (p && hit && hit.level === p.level && hit.index === p.index)
                root.activate(p.level, p.index)
        }
    }

    FontMetrics {
        id: metrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    contentItem: Item {
        Item {
            id: keySink
            Keys.onPressed: (event) => root.handleKey(event)
        }

        Repeater {
            id: columns
            model: root.maxDepth

            delegate: Rectangle {
                id: col

                required property int index
                property var entries: []
                property real contentH: 0
                property real colW: 0
                property bool hasIcons: false
                property int hover: -1
                property int openIndex: -1
                property bool flipped: false
                property bool shown: false
                property alias contentY: flick.contentY
                readonly property real viewHeight: flick.height
                readonly property real headerH: index === 0 && root.searchable
                                                ? Theme.controlHeight + Theme.spacingSm : 0

                visible: shown
                width: colW
                height: Math.min(headerH + contentH + 2 * root.pad, root.height - 2 * root.edgeMargin)
                radius: Theme.c.themedContextMenu.radius
                color: Theme.c.themedContextMenu.background
                border.width: Theme.c.themedContextMenu.borderWidth
                border.color: Theme.c.themedContextMenu.border

                onShownChanged: {
                    if (shown)
                        fadeIn.restart()
                }

                NumberAnimation {
                    id: fadeIn
                    target: col
                    property: "opacity"
                    from: 0.0
                    to: 1.0
                    duration: Theme.durationFast
                    easing.type: Theme.easing
                }

                Loader {
                    active: col.index === 0 && root.searchable
                    x: root.pad
                    y: root.pad
                    width: col.width - 2 * root.pad

                    sourceComponent: ThemedTextField {
                        id: field
                        placeholderText: root.searchPlaceholder
                        onTextChanged: root.query = text
                        Keys.onPressed: (event) => root.handleKey(event)
                        Component.onCompleted: root.searchField = field
                    }
                }

                Flickable {
                    id: flick
                    x: root.pad
                    y: root.pad + col.headerH
                    width: col.width - 2 * root.pad
                    height: col.height - 2 * root.pad - col.headerH
                    contentHeight: col.contentH
                    interactive: false
                    clip: true

                    Repeater {
                        model: col.entries

                        delegate: Item {
                            id: row

                            required property var modelData
                            required property int index
                            readonly property bool usable: root.selectable(modelData)
                            readonly property bool lit: usable && (index === col.hover || index === col.openIndex)

                            y: modelData._y
                            width: flick.width
                            height: modelData._h

                            Rectangle {
                                anchors.fill: parent
                                visible: !row.modelData.separator
                                radius: Theme.c.themedMenuItem.radius
                                color: row.lit ? Theme.c.themedMenuItem.backgroundHighlighted : "transparent"

                                Behavior on color {
                                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }
                            }

                            Rectangle {
                                visible: !!row.modelData.separator
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                height: Theme.c.themedMenuSeparator.borderWidth
                                color: Theme.c.themedMenuSeparator.background
                            }

                            IconGlyph {
                                x: Theme.spacingLg
                                anchors.verticalCenter: parent.verticalCenter
                                visible: !!row.modelData.icon && !row.modelData.separator
                                glyph: row.modelData.icon || ""
                                iconSize: Theme.iconSizeMd
                                iconColor: row.usable ? Theme.c.themedMenuItem.foreground
                                                      : Theme.c.themedMenuItem.foregroundDisabled
                                opacity: row.usable ? 1 : 0.5
                            }

                            Text {
                                x: Theme.spacingLg + (col.hasIcons ? Theme.iconSizeMd + Theme.spacingLg : 0)
                                anchors.verticalCenter: parent.verticalCenter
                                width: (hint.visible ? hint.x - Theme.spacingLg
                                                     : arrow.visible ? arrow.x - Theme.spacingSm
                                                                     : row.width - Theme.spacingLg) - x
                                visible: !row.modelData.separator
                                text: row.modelData.text || ""
                                elide: Text.ElideRight
                                color: row.modelData.header ? Theme.c.themedMenuItem.sectionForeground
                                                            : Theme.c.themedMenuItem.foreground
                                opacity: row.modelData.header || row.usable ? 1 : 0.5
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                font.weight: row.modelData.header ? Font.DemiBold : Font.Normal
                            }

                            Text {
                                id: hint
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.spacingLg
                                anchors.verticalCenter: parent.verticalCenter
                                visible: !!row.modelData.hint
                                width: Math.min(implicitWidth, row.width * 0.45)
                                text: row.modelData.hint || ""
                                elide: Text.ElideLeft
                                color: Theme.c.themedMenuItem.sectionForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }

                            IconGlyph {
                                id: arrow
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.spacingSm
                                anchors.verticalCenter: parent.verticalCenter
                                visible: root.hasChildren(row.modelData)
                                glyph: Theme.icons.chevronRight
                                iconSize: Theme.iconSizeMd
                                iconColor: Theme.c.themedMenuItem.icon
                                opacity: row.usable ? 1 : 0.5
                            }
                        }
                    }
                }
            }
        }

        // Above the columns so every move reaches pointerMoved, whichever column is under it.
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: root.cursor

            onPositionChanged: (mouse) => root.pointerMoved(mouse.x, mouse.y)
            onPressed: (mouse) => {
                const hit = root.hitTest(mouse.x, mouse.y)
                if (!hit)
                    root.close()
                else if (hit.inHeader)
                    mouse.accepted = false
            }
            onClicked: (mouse) => {
                if (mouse.button !== Qt.LeftButton)
                    return
                const hit = root.hitTest(mouse.x, mouse.y)
                if (hit && hit.index >= 0)
                    root.trigger(hit.level, hit.index)
            }
            onWheel: (wheel) => {
                const hit = root.hitTest(wheel.x, wheel.y)
                if (hit)
                    root.scrollColumn(hit.level, -wheel.angleDelta.y / 120 * 3 * root.itemHeight)
            }
        }
    }
}

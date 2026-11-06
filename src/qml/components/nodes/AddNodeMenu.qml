pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Prism

// Searchable palette of everything that can be added to the graph: source kinds and node types,
// grouped by category, with fuzzy search over names and aliases and a Recent section. Used by the
// node editor (canvas set) and, restricted to sources, by the Assets panel and the File menu.
// Opened from a connection dropped on empty space, pendingKind is the dragged port's PortKind and
// only types with a compatible port on the opposite side are listed. Opened over a wire, edgeId
// is set and only types that fit between the wire's two ends are listed; the pick is spliced in.
Popup {
    id: root

    property var canvas: null
    // -1 for a plain add; otherwise the PortKind of the dragged port.
    property int pendingKind: -1
    property bool pendingIsOutput: false
    // Where the node is created, in world coordinates. NaN lets the graph place it.
    property point worldPos: Qt.point(NaN, NaN)
    property bool wasPicked: false
    property bool sourcesOnly: false
    property var edgeId: 0

    function openAt(sx, sy) {
        wasPicked = false
        search.text = ""
        rebuild()
        if (canvas && edgeId)
            canvas.spliceEdgeId = edgeId
        x = Math.max(8, Math.min(sx, (parent ? parent.width : sx) - width - 8))
        y = Math.max(8, Math.min(sy, (parent ? parent.height : sy) - height - 8))
        open()
    }

    function openSourcesBelow(item) {
        sourcesOnly = true
        pendingKind = -1
        edgeId = 0
        worldPos = Qt.point(NaN, NaN)
        const p = item.mapToItem(parent, 0, item.height)
        openAt(p.x, p.y)
    }

    property var rows: []
    property int highlighted: -1

    readonly property var aliases: ({
        "file": ["mp4", "clip", "movie", "video", "mov", "image", "photo", "picture", "audio", "mp3", "wav", "file", "import"],
        "slideshow": ["images", "photos", "pictures", "folder", "gallery"],
        "svgTemplate": ["svg", "graphic", "lower third", "title"],
        "camera": ["webcam", "cam", "capture card", "video input"],
        "MasterAudioInput": ["microphone", "mic", "audio input"],
        "MasterAudioCapture": ["system audio", "desktop audio", "loopback"],
        "screen": ["display", "desktop", "monitor"],
        "window": ["app", "application"],
        "canvas": ["solid", "color", "background", "checkered"],
        "shader": ["glsl", "generator", "effect"],
        "text": ["title", "caption", "label", "subtitle"],
        "shape": ["rectangle", "circle", "box"],
        "url": ["stream", "http", "rtmp", "hls", "rtsp", "link", "network"],
        "ndi": ["network", "newtek"],
        "webrtc": ["phone", "mobile", "webrtc", "smartphone"],
        "crop": ["trim", "cut", "region"],
        "flip h": ["mirror", "horizontal"],
        "flip v": ["vertical"],
        "flip": ["mirror"],
        "remove bg": ["background", "matte", "cutout", "segment"],
        "rotate": ["turn", "spin"],
        "opacity": ["transparency", "alpha", "fade"],
        "chroma key": ["green", "greenscreen", "green screen", "keyer"],
        "blur": ["soften", "gaussian"],
        "sharpen": ["crisp", "detail"],
        "keystone": ["perspective", "warp", "skew"],
        "poly mask": ["mask", "polygon"],
        "Layer": ["composite", "overlay", "stack", "pip"],
        "AbSelect": ["switch", "mix", "cut", "transition", "ab"],
        "audio fx": ["pedalboard", "effects", "reverb", "eq", "compressor"],
        "MasterAudioOutput": ["speaker", "master", "device"],
        "AudioMixer": ["mix", "volume", "sum"],
        "Script": ["lua", "code", "program"],
        "AudioScript": ["lua", "dsp", "code"],
        "Trigger": ["timer", "pulse", "event", "clock"]
    })

    function allEntries() {
        const T = NodePalette.nodeType
        const list = []
        function src(group, kind, label, avail) {
            if (avail !== false)
                list.push({ key: "src:" + kind, alias: kind, label: label,
                            category: qsTr("Sources: %1").arg(group), source: kind })
        }
        function node(category, key, label, type, params, unique) {
            list.push({ key: key, alias: Object.keys(T).find(k => T[k] === type), label: label,
                        category: category, type: type, params: params, unique: unique })
        }
        src(qsTr("File"), "file", qsTr("Media File"))
        src(qsTr("File"), "slideshow", qsTr("Slideshow"))
        src(qsTr("File"), "svgTemplate", qsTr("SVG Template"))
        src(qsTr("Live"), "camera", qsTr("Camera"))
        node(qsTr("Sources: Live"), "type:" + T.MasterAudioInput, qsTr("Mic Input"), T.MasterAudioInput)
        node(qsTr("Sources: Live"), "type:" + T.MasterAudioCapture, qsTr("Audio Capture"), T.MasterAudioCapture)
        src(qsTr("Live"), "screen", qsTr("Screen Capture"))
        src(qsTr("Live"), "window", qsTr("Window Capture"))
        src(qsTr("Generated"), "canvas", qsTr("Canvas"))
        src(qsTr("Generated"), "shader", qsTr("Shader"))
        src(qsTr("Generated"), "text", qsTr("Text"))
        src(qsTr("Generated"), "shape", qsTr("Shape"))
        src(qsTr("Network"), "url", qsTr("Video URL"))
        src(qsTr("Network"), "ndi", qsTr("NDI Source"), SourcePrompts.ndiAvailable)
        src(qsTr("Network"), "webrtc", qsTr("Phone Camera (WebRTC)"), SourcePrompts.webrtcAvailable)
        if (sourcesOnly)
            return list
        for (const fx of NodeCatalog.processEffects())
            list.push({ key: "fx:" + fx.id, alias: fx.name.toLowerCase(), label: fx.menuLabel,
                        category: qsTr("Video FX"), type: T.Process, params: { effect: fx.id } })
        node(qsTr("Compositing"), "type:" + T.Layer, qsTr("Layer"), T.Layer)
        node(qsTr("Compositing"), "type:" + T.AbSelect, qsTr("Switcher"), T.AbSelect)
        for (const fx of NodeCatalog.audioEffects())
            list.push({ key: "afx:" + fx.id, alias: "audio fx", label: fx.menuLabel,
                        category: qsTr("Audio"), type: T.AudioEffect, params: { effect: fx.id } })
        node(qsTr("Audio"), "type:" + T.AudioMixer, qsTr("Mixer"), T.AudioMixer)
        node(qsTr("Audio"), "type:" + T.MasterAudioOutput, qsTr("Audio Output"), T.MasterAudioOutput, undefined, true)
        if (NodeCatalog.scriptingAvailable()) {
            node(qsTr("Logic"), "type:" + T.Script, qsTr("Script"), T.Script)
            node(qsTr("Logic"), "type:" + T.AudioScript, qsTr("Audio Script"), T.AudioScript)
            node(qsTr("Logic"), "type:" + T.Trigger, qsTr("Trigger"), T.Trigger)
        }
        return list
    }

    function compatible(entry) {
        if (edgeId) {
            if (entry.source || !canvas)
                return false
            const ends = canvas.edgeLayer.endKinds(edgeId)
            if (!ends)
                return false
            const m = NodePalette.meta[entry.type]
            return m.inKinds.some(k => NodeCatalog.portsCompatible(ends[0], k))
                   && m.outKinds.some(k => NodeCatalog.portsCompatible(k, ends[1]))
        }
        if (pendingKind < 0)
            return true
        if (entry.source)
            return false
        const meta = NodePalette.meta[entry.type]
        const kinds = pendingIsOutput ? meta.inKinds : meta.outKinds
        return kinds.some(k => NodeCatalog.portsCompatible(pendingKind, k))
    }

    function isSubsequence(q, t) {
        let i = 0
        for (let j = 0; j < t.length && i < q.length; ++j) {
            if (t[j] === q[i])
                ++i
        }
        return i === q.length
    }

    // Prefix matches outrank word-prefix, substring and subsequence matches; aliases rank below names.
    function score(q, entry) {
        const names = [entry.label.toLowerCase()].concat(aliases[entry.alias] || [])
        let best = -1
        for (let i = 0; i < names.length; ++i) {
            const t = names[i]
            let s = -1
            if (t.startsWith(q))
                s = 100
            else if (t.split(/[^a-z0-9]+/).some(w => w.startsWith(q)))
                s = 85
            else if (t.indexOf(q) >= 0)
                s = 60
            else if (q.length >= 2 && isSubsequence(q, t))
                s = 30
            if (s >= 0)
                best = Math.max(best, i === 0 ? s : s - 10)
        }
        if (best < 0 && entry.category.toLowerCase().indexOf(q) >= 0)
            best = 5
        return best
    }

    function rebuild() {
        const q = search.text.trim().toLowerCase()
        const eligible = allEntries().filter(e => !(e.unique && canvas && canvas.hasNodeOfType(e.type))
                                                  && compatible(e))
        const out = []
        if (q.length > 0) {
            const scored = []
            for (const e of eligible) {
                const sc = score(q, e)
                if (sc >= 0)
                    scored.push({ e: e, sc: sc, i: scored.length })
            }
            scored.sort((a, b) => b.sc - a.sc || a.i - b.i)
            for (const r of scored)
                out.push(Object.assign({ hint: r.e.category }, r.e))
        } else {
            const byKey = {}
            for (const e of eligible)
                byKey[e.key] = e
            const recent = NodeCatalog.recentNodeKeys().filter(k => byKey[k]).map(k => byKey[k])
            if (recent.length > 0) {
                out.push({ header: true, label: qsTr("Recent") })
                for (const e of recent)
                    out.push(Object.assign({ hint: e.category }, e))
            }
            let lastCategory = ""
            for (const e of eligible) {
                if (e.category !== lastCategory) {
                    out.push({ header: true, label: e.category })
                    lastCategory = e.category
                }
                out.push(e)
            }
        }
        rows = out
        highlighted = -1
        for (let i = 0; i < out.length; ++i) {
            if (!out[i].header) {
                highlighted = i
                break
            }
        }
    }

    function step(delta) {
        let i = highlighted
        for (;;) {
            i += delta
            if (i < 0 || i >= rows.length)
                return
            if (!rows[i].header)
                break
        }
        highlighted = i
        list.positionViewAtIndex(i, ListView.Contain)
    }

    function pick(entry) {
        wasPicked = true
        close()
        NodeCatalog.noteNodeAdded(entry.key)
        const x = worldPos.x
        const y = worldPos.y - 28
        if (entry.source) {
            if (canvas)
                canvas.cancelConnection()
            if (entry.source === "file")
                AppController.requestAddFiles(x, y)
            else
                AppController.requestAddSource(entry.source, x, y)
        } else if (canvas) {
            canvas.addNodeAt(entry.type, entry.params, worldPos.x, worldPos.y, edgeId)
        } else {
            AppController.graph.addNode(entry.type, NaN, NaN, entry.params || {})
        }
    }

    width: 280
    height: Math.min(360, spacer.height + list.contentHeight + padding * 2 + Theme.spacingSm)
    padding: Theme.spacingSm
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onOpened: search.forceActiveFocus()
    onClosed: {
        if (canvas)
            canvas.spliceEdgeId = 0
        sourcesOnly = false
    }

    background: Rectangle {
        radius: Theme.c.themedContextMenu.radius
        color: Theme.c.themedContextMenu.background
        border.width: Theme.c.themedContextMenu.borderWidth
        border.color: Theme.c.themedContextMenu.border
    }

    contentItem: Item {
        ThemedTextField {
            id: search
            width: parent.width
            placeholderText: root.pendingKind >= 0 ? qsTr("Add and connect…") : qsTr("Search nodes…")
            onTextChanged: root.rebuild()
            Keys.onDownPressed: root.step(1)
            Keys.onUpPressed: root.step(-1)
            Keys.onTabPressed: (event) => event.accepted = true
            Keys.onReturnPressed: {
                if (root.highlighted >= 0)
                    root.pick(root.rows[root.highlighted])
            }
            Keys.onEnterPressed: {
                if (root.highlighted >= 0)
                    root.pick(root.rows[root.highlighted])
            }
        }

        Item {
            id: spacer
            width: parent.width
            height: search.height + Theme.spacingSm
        }

        NodeLabel {
            x: Theme.spacingLg
            y: spacer.height + Theme.spacingSm
            visible: root.rows.length === 0
            muted: true
            text: qsTr("No matching nodes")
        }

        ListView {
            id: list
            y: spacer.height
            width: parent.width
            height: parent.height - spacer.height
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: row

                required property var modelData
                required property int index

                width: ListView.view.width
                height: row.modelData.header ? Theme.controlHeightSm : Theme.controlHeightSm + Theme.spacingSm

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.c.themedMenuItem.radius
                    color: !row.modelData.header && (area.containsMouse || root.highlighted === row.index)
                           ? Theme.c.themedMenuItem.backgroundHighlighted : "transparent"
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.spacingLg
                    width: parent.width - 2 * Theme.spacingLg - hint.width
                    text: row.modelData.label
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: row.modelData.header ? Font.DemiBold : Font.Normal
                    color: row.modelData.header ? Theme.c.themedMenuItem.sectionForeground
                                                : Theme.c.themedMenuItem.foreground
                }

                Text {
                    id: hint
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingLg
                    visible: !!row.modelData.hint
                    width: visible ? implicitWidth : 0
                    text: row.modelData.hint || ""
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.c.themedMenuItem.sectionForeground
                }

                MouseArea {
                    id: area
                    anchors.fill: parent
                    enabled: !row.modelData.header
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: root.highlighted = row.index
                    onClicked: root.pick(row.modelData)
                }
            }
        }
    }
}

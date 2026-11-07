pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Prism

// Everything that can be added to the graph (source kinds and node types) as a cascading menu
// grouped by category, with fuzzy search over names and aliases and a Recent submenu. Used by the
// node editor (canvas set) and, restricted to sources, by the Assets panel and the File menu.
// Opened from a connection dropped on empty space, pendingKind is the dragged port's PortKind and
// only types with a compatible port on the opposite side are listed. Opened over a wire, edgeId
// is set and only types that fit between the wire's two ends are listed; the pick is spliced in.
CascadeMenu {
    id: root

    property var canvas: null
    // -1 for a plain add; otherwise the PortKind of the dragged port.
    property int pendingKind: -1
    property bool pendingIsOutput: false
    // Where the node is created, in world coordinates. NaN lets the graph place it.
    property point worldPos: Qt.point(NaN, NaN)
    readonly property bool wasPicked: triggered
    property bool sourcesOnly: false
    property var edgeId: 0

    property var eligible: []
    property var categoryIcons: ({})

    searchable: true
    searchPlaceholder: pendingKind >= 0 ? qsTr("Add and connect…") : qsTr("Search nodes…")
    searchFn: (q) => root.search(q)

    // x/y are in `item`'s coordinates.
    function openAt(item, x, y) {
        eligible = allEntries().filter(e => !(e.unique && canvas && canvas.hasNodeOfType(e.type))
                                            && compatible(e))
        model = buildTree()
        if (canvas && edgeId)
            canvas.spliceEdgeId = edgeId
        popupAt(item, x, y)
    }

    function openSourcesBelow(item) {
        sourcesOnly = true
        pendingKind = -1
        edgeId = 0
        worldPos = Qt.point(NaN, NaN)
        openAt(item, 0, item.height)
    }

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
        "shader": ["glsl", "shader", "custom", "lua", "visualizer", "effect"],
        "generator": ["generate", "procedural", "gradient", "noise", "pattern", "shape", "spectrum"],
        "text": ["title", "caption", "label", "subtitle"],
        "shape": ["rectangle", "circle", "box"],
        "scene": ["nested", "embed", "picture in picture"],
        "sceneInput": ["post", "processing", "input", "wrap", "filter"],
        "url": ["stream", "http", "rtmp", "hls", "rtsp", "link", "network"],
        "ndi": ["network", "newtek"],
        "webrtc": ["phone", "mobile", "webrtc", "smartphone"],
        "shaderFx": ["glsl", "shader", "custom"],
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
        "audio fx": ["pedalboard", "effects", "reverb", "eq", "compressor"],
        "MasterAudioOutput": ["speaker", "master", "device"],
        "AudioMixer": ["mix", "volume", "sum"],
        "Script": ["lua", "code", "program", "timer", "pulse", "trigger", "clock"],
        "AudioAnalyzer": ["fft", "beat", "spectrum", "dsp"],
        "Frame": ["backdrop", "comment", "note", "sticky", "label", "annotation"],
        "Reroute": ["dot", "knot", "waypoint", "wire", "cable", "bend"],
        "Modulator": ["lfo", "envelope", "random", "bpm", "tempo", "counter", "toggle", "math", "smooth", "link"]
    })

    // Each entry's path is the chain of submenus it sits in; section groups top-level menus
    // between separators.
    function allEntries() {
        const T = NodePalette.nodeType
        const list = []
        const icons = {}
        function cat(name, icon) {
            icons[name] = icon
            return name
        }
        const fileCat = cat(qsTr("File"), "file")
        const liveCat = cat(qsTr("Live"), "radio")
        const genCat = cat(qsTr("Generated"), "sparkles")
        const procCat = cat(qsTr("Generators"), "wand-sparkles")
        const netCat = cat(qsTr("Network"), "globe")
        function src(path, kind, label, icon, avail) {
            if (avail !== false)
                list.push({ key: "src:" + kind, alias: kind, label: label, icon: icon, path: path,
                            section: 0, source: kind })
        }
        function node(path, section, key, label, icon, type, params, unique) {
            list.push({ key: key, alias: Object.keys(T).find(k => T[k] === type), label: label, icon: icon,
                        path: path, section: section, type: type, params: params, unique: unique })
        }
        src([fileCat], "file", qsTr("Media File"), "file-play")
        src([fileCat], "slideshow", qsTr("Slideshow"), "images")
        src([fileCat], "svgTemplate", qsTr("SVG Template"), "file-code")
        src([liveCat], "camera", qsTr("Camera"), "camera")
        node([liveCat], 0, "type:" + T.MasterAudioInput, qsTr("Mic Input"), "mic", T.MasterAudioInput)
        node([liveCat], 0, "type:" + T.MasterAudioCapture, qsTr("Audio Capture"), "audio-lines", T.MasterAudioCapture)
        src([liveCat], "screen", qsTr("Screen Capture"), "monitor")
        src([liveCat], "window", qsTr("Window Capture"), "app-window")
        src([genCat], "canvas", qsTr("Canvas"), "square")
        src([genCat], "text", qsTr("Text"), "type")
        src([genCat], "shape", qsTr("Shape"), "shapes")
        src([genCat], "scene", qsTr("Scene"), "clapperboard")
        src([genCat], "sceneInput", qsTr("Scene input"), "log-in",
            AppController.postSceneId.length > 0 && AppController.editedSceneId === AppController.postSceneId)
        src([genCat, procCat], "shader", qsTr("Custom Generator"), "code-xml")
        for (const g of NodeCatalog.generators())
            list.push({ key: "src:generator:" + g.slug, alias: "generator", label: g.name,
                        path: [genCat, procCat], section: 0, source: "generator:" + g.slug })
        src([netCat], "url", qsTr("Video URL"), "link")
        src([netCat], "ndi", qsTr("NDI Source"), "cast", SourcePrompts.ndiAvailable)
        src([netCat], "webrtc", qsTr("Phone Camera (WebRTC)"), "smartphone", SourcePrompts.webrtcAvailable)
        if (!sourcesOnly) {
            const fxCat = cat(qsTr("Video FX"), "blend")
            const audioCat = cat(qsTr("Audio"), "audio-waveform")
            const logicCat = cat(qsTr("Logic"), "binary")
            const utilCat = cat(qsTr("Utility"), "wrench")
            const fxIcons = { "Transform": "move", "Keying": "pipette", "Color": "palette", "Blur": "droplet",
                              "Distort": "waves-horizontal", "Composite": "layers", "Light": "sun",
                              "Stylize": "brush", "Time": "clock" }
            const fxGroup = {}
            for (const e of NodeCatalog.quickAddEntries("", -1, false)) {
                if (e.nodeType === T.Process)
                    fxGroup[e.params.effect] = e.category
            }
            const shaderFx = NodeCatalog.processEffects().find(fx => fx.id === 67)
            if (shaderFx)
                list.push({ key: "fx:" + shaderFx.id, alias: "shaderFx", label: shaderFx.menuLabel, icon: "code-xml",
                            path: [fxCat], section: 1, type: T.Process, params: { effect: shaderFx.id } })
            for (const fx of NodeCatalog.processEffects()) {
                if (fx.id === 67)
                    continue
                const group = fxGroup[fx.id] || qsTr("Other")
                if (fxIcons[group])
                    icons[group] = fxIcons[group]
                list.push({ key: "fx:" + fx.id, alias: fx.name.toLowerCase(), label: fx.menuLabel,
                            path: [fxCat, group], section: 1, type: T.Process, params: { effect: fx.id } })
            }
            node([], 1, "type:" + T.Layer, qsTr("Layer"), "layers", T.Layer)
            for (const fx of NodeCatalog.audioEffects())
                list.push({ key: "afx:" + fx.id, alias: "audio fx", label: fx.menuLabel, icon: "sliders-vertical",
                            path: [audioCat], section: 1, type: T.AudioEffect, params: { effect: fx.id } })
            node([audioCat], 1, "type:" + T.AudioMixer, qsTr("Mixer"), "sliders-horizontal", T.AudioMixer)
            node([audioCat], 1, "type:" + T.MasterAudioOutput, qsTr("Audio Output"), "speaker",
                 T.MasterAudioOutput, undefined, true)
            if (NodeCatalog.scriptingAvailable()) {
                node([logicCat], 2, "type:" + T.Script, qsTr("Script"), "code-xml", T.Script)
                node([logicCat], 2, "type:" + T.AudioAnalyzer, qsTr("Audio Analyzer"), "activity", T.AudioAnalyzer)
                node([logicCat], 2, "type:" + T.Modulator, qsTr("Modulator"), "spline", T.Modulator)
            }
            node([utilCat], 2, "type:" + T.Reroute, qsTr("Reroute"), "dot", T.Reroute)
            list.push({ key: "frame", alias: "Frame", label: qsTr("Frame"), icon: "frame", path: [utilCat],
                        section: 2, frame: true })
        }
        categoryIcons = icons
        return list
    }

    function item(entry, hint) {
        return { text: entry.label, icon: entry.icon, hint: hint, action: () => root.pick(entry) }
    }

    function buildTree() {
        const top = []
        const branches = {}
        let lastSection = -1
        function addTop(n, section) {
            if (top.length > 0 && section !== lastSection)
                top.push({ separator: true })
            lastSection = section
            top.push(n)
        }
        for (const e of eligible) {
            let list = null
            let key = ""
            for (const name of e.path) {
                key += "\u0001" + name
                let b = branches[key]
                if (!b) {
                    b = { text: name, icon: categoryIcons[name] || "", children: [] }
                    branches[key] = b
                    if (list)
                        list.push(b)
                    else
                        addTop(b, e.section)
                }
                list = b.children
            }
            if (list)
                list.push(item(e))
            else
                addTop(item(e), e.section)
        }
        const byKey = {}
        for (const e of eligible)
            byKey[e.key] = e
        const recent = NodeCatalog.recentNodeKeys().filter(k => byKey[k]).map(k => item(byKey[k]))
        if (recent.length > 0)
            top.unshift({ text: qsTr("Recent"), icon: "history", children: recent }, { separator: true })
        if (top.length === 0)
            top.push({ header: true, text: qsTr("No matching nodes") })
        return top
    }

    function search(q) {
        q = q.toLowerCase()
        const scored = []
        for (const e of eligible) {
            const sc = score(q, e)
            if (sc >= 0)
                scored.push({ e: e, sc: sc, i: scored.length })
        }
        scored.sort((a, b) => b.sc - a.sc || a.i - b.i)
        const out = scored.map(r => item(r.e, r.e.path.join(" › ")))
        return out.length > 0 ? out : [{ header: true, text: qsTr("No matching nodes") }]
    }

    function compatible(entry) {
        if (entry.frame)
            return pendingKind < 0 && !edgeId
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
        if (best < 0 && entry.path.join(" ").toLowerCase().indexOf(q) >= 0)
            best = 5
        return best
    }

    function pick(entry) {
        NodeCatalog.noteNodeAdded(entry.key)
        const x = worldPos.x
        const y = worldPos.y - 28
        if (entry.frame) {
            if (canvas)
                canvas.addFrameAt(worldPos.x, worldPos.y)
        } else if (entry.source) {
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

    onClosed: {
        if (canvas)
            canvas.spliceEdgeId = 0
        sourcesOnly = false
    }
}

pragma Singleton
import QtQuick
import Prism

// Colours, node-type metadata and enum mirrors for the node editor. The integer tables mirror
// ClipGraph::NodeType / PortKind / EdgeKind (declaration order; the EdgeKind values are persisted).
QtObject {
    id: root

    readonly property var nodeType: ({
        Input: 0, Process: 1, AudioEffect: 2, Layer: 3, Output: 4, AudioMixer: 5,
        MasterAudioOutput: 6, MasterAudioInput: 7, MasterAudioCapture: 8, Script: 9,
        AudioAnalyzer: 10, Modulator: 11, Reroute: 12
    })

    readonly property var portKind: ({
        ChainIn: 0, ChainOut: 1, AudioOut: 2, ShaderAudioIn: 3, AudioIn: 4,
        AudioControllerOut: 5, MasterAudioIn: 6, MasterAudioInputOut: 7, ScriptOut: 8, DataIn: 9,
        AudioMixerIn: 10, AudioMixerOut: 11, AudioEffectIn: 12, AudioEffectOut: 13, RerouteIn: 14, RerouteOut: 15,
        ParamIn: 16
    })

    // The same list the scene tabs use; "" is no colour.
    readonly property var frameColors: ["", "#e5484d", "#f76b15", "#ffc53d", "#30a46c", "#0090ff", "#8e4ec6"]

    readonly property var edgeKind: ({
        Chain: 0, TransformToContext: 1, AudioToController: 2,
        ClipToShaderAudio: 5, ScriptToData: 6, InputToMaster: 7,
        StreamToMixer: 9, MixerToOutput: 10, AudioEffectChain: 11,
        ClipToAudioAnalyzer: 12
    })

    // Domain hues. These carry meaning (video / raw audio / mixed audio / script data) and
    // have no counterpart in the theme tokens, so they stay fixed;
    readonly property color chainHue: "#50a8d8"
    readonly property color streamHue: "#e4b042"
    readonly property color mixedHue: "#e88030"
    readonly property color scriptHue: "#70c0a8"

    function desat(value, keep) {
        const c = Qt.color(value)
        return Qt.hsva(c.hsvHue < 0 ? 0 : c.hsvHue, c.hsvSaturation * keep, c.hsvValue, c.a)
    }

    function portColor(kind) {
        const k = portKind
        switch (kind) {
        case k.ChainIn:
        case k.ChainOut:
            return desat(chainHue, 0.8)
        case k.AudioOut:
        case k.ShaderAudioIn:
        case k.AudioIn:
        case k.MasterAudioIn:
        case k.AudioMixerOut:
            return desat(mixedHue, 0.8)
        case k.AudioControllerOut:
        case k.MasterAudioInputOut:
        case k.AudioMixerIn:
        case k.AudioEffectIn:
        case k.AudioEffectOut:
            return desat(streamHue, 0.8)
        case k.ScriptOut:
        case k.DataIn:
        case k.ParamIn:
            return desat(scriptHue, 0.8)
        }
        return Theme.mutedForeground
    }

    function portKindName(kind) {
        const k = portKind
        switch (kind) {
        case k.ChainIn: case k.ChainOut: return qsTr("Video")
        case k.AudioControllerOut: case k.MasterAudioInputOut: return qsTr("Audio stream")
        case k.AudioMixerIn: return qsTr("Mixer input")
        case k.AudioMixerOut: return qsTr("Mixed audio")
        case k.AudioEffectIn: case k.AudioEffectOut: return qsTr("Audio effect")
        case k.MasterAudioIn: return qsTr("Audio output")
        case k.AudioIn: case k.ShaderAudioIn: case k.AudioOut: return qsTr("Audio")
        case k.ScriptOut: case k.DataIn: return qsTr("Script data")
        case k.ParamIn: return qsTr("Parameter")
        case k.RerouteIn: case k.RerouteOut: return qsTr("Any")
        }
        return ""
    }

    function edgeColor(kind) {
        const e = edgeKind
        switch (kind) {
        case e.Chain: return desat("#70b8ff", 0.8)
        case e.AudioToController: return desat(mixedHue, 0.8)
        case e.ClipToShaderAudio: return desat("#e89040", 0.8)
        case e.ClipToAudioAnalyzer:
        case e.StreamToMixer:
        case e.AudioEffectChain:
            return desat(streamHue, 0.8)
        case e.MixerToOutput:
        case e.InputToMaster:
            return desat(mixedHue, 0.8)
        case e.ScriptToData: return desat("#70c8a0", 0.8)
        }
        return desat("#f0c050", 0.8)
    }

    // Per node type: display name, header glyph (Lucide), header hue, header height in node
    // coordinates (matches the layout constants in ClipGraph), add-menu category and the port
    // kinds a freshly added node carries (used to filter the add menu on an edge drop).
    readonly property var meta: [
        { name: qsTr("Input"), icon: "clapperboard", hue: "#78aad2", header: 20, category: "source",
          inKinds: [], outKinds: [1, 5] },
        { name: qsTr("Process"), icon: "wand-sparkles", hue: "#78aad2", header: 20, category: "video",
          inKinds: [0], outKinds: [1] },
        { name: qsTr("Audio FX"), icon: "audio-waveform", hue: "#e4b042", header: 20, category: "audio",
          inKinds: [12], outKinds: [13] },
        { name: qsTr("Layer"), icon: "layers", hue: "#78aad2", header: 20, category: "video",
          inKinds: [0], outKinds: [1] },
        { name: qsTr("Output"), icon: "cast", hue: "#96c8f0", header: 20, category: "video",
          inKinds: [0], outKinds: [] },
        { name: qsTr("Mixer"), icon: "sliders-vertical", hue: "#e4b042", header: 20, category: "audio",
          inKinds: [10], outKinds: [11] },
        { name: qsTr("Audio Output"), icon: "speaker", hue: "#e88030", header: 22, category: "audio",
          inKinds: [6], outKinds: [] },
        { name: qsTr("Mic Input"), icon: "mic", hue: "#e4b042", header: 20, category: "audio",
          inKinds: [], outKinds: [7] },
        { name: qsTr("Audio Capture"), icon: "audio-lines", hue: "#e4b042", header: 20, category: "audio",
          inKinds: [], outKinds: [7] },
        { name: qsTr("Script"), icon: "binary", hue: "#78dcaa", header: 20, category: "script",
          inKinds: [9], outKinds: [8] },
        { name: qsTr("Audio Analyzer"), icon: "activity", hue: "#e6be5a", header: 20, category: "script",
          inKinds: [4], outKinds: [8] },
        { name: qsTr("Modulator"), icon: "audio-waveform", hue: "#78dcaa", header: 20, category: "script",
          inKinds: [], outKinds: [8] },
        { name: qsTr("Reroute"), icon: "circle", hue: "#8a8f98", header: 0, category: "utility",
          inKinds: [14], outKinds: [15] }
    ]

    // Default node width per type, used to place a node so its port lands under the cursor.
    readonly property var defaultWidth: [122, 116, 116, 158, 116, 158, 168, 100, 100, 100, 100, 100, 22]

    function tooltip(type) {
        switch (type) {
        case nodeType.Layer:
            return qsTr("Double-click a layer name to rename. The eye toggles visibility; the arrows reorder.")
        case nodeType.AudioMixer:
            return qsTr("Double-click a name to rename. Select the node to set per-input volume and mute.")
        }
        return ""
    }

    // SourceDescriptor::Kind order.
    readonly property var kindIcons: ["film", "image", "folder", "camera", "screen-share",
        "square-dashed", "app-window", "sparkles", "film", "radio-receiver", "smartphone", "type",
        "music", "shapes", "layout-grid", "clapperboard", "sparkles"]

    function sourceIcon(source) {
        return source && kindIcons[source.kind] ? kindIcons[source.kind] : meta[nodeType.Input].icon
    }

    // Name of the editor the inspector's "Open editor" button asks the shell for, "" if the node
    // is fully editable in the inspector.
    function editorKind(type, params) {
        switch (type) {
        case nodeType.Input:
            switch (params.source ? params.source.kind : -1) {
            case 7: return params.source.preset ? "" : "shader"
            case 11: return "text"
            case 13: return "shape"
            case 14: return "svgTemplate"
            }
            return ""
        case nodeType.Process:
            if (params.effect === 67) return "shaderEffect"
            return [0, 10, 11].indexOf(params.effect) >= 0 ? "process" : ""
        case nodeType.AudioEffect: return "audioEffect"
        case nodeType.Layer: return "layerLayout"
        case nodeType.Script: return "script"
        }
        return ""
    }
}

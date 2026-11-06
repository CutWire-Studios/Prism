pragma Singleton
import QtQuick
import Prism

// Colours, node-type metadata and enum mirrors for the node editor. The integer tables mirror
// ClipGraph::NodeType / PortKind / EdgeKind (declaration order; the EdgeKind values are persisted).
QtObject {
    id: root

    readonly property var nodeType: ({
        Input: 0, Process: 1, AudioEffect: 2, Layer: 3, AbSelect: 4, Output: 5, AudioMixer: 6,
        MasterAudioOutput: 7, MasterAudioInput: 8, MasterAudioCapture: 9, Script: 10,
        AudioScript: 11, Trigger: 12
    })

    readonly property var portKind: ({
        ChainIn: 0, ChainOut: 1, AbOut: 2, AbIn: 3, AudioOut: 4, ShaderAudioIn: 5, AudioIn: 6,
        AudioControllerOut: 7, MasterAudioIn: 8, MasterAudioInputOut: 9, ScriptOut: 10, DataIn: 11,
        AudioMixerIn: 12, AudioMixerOut: 13, AudioEffectIn: 14, AudioEffectOut: 15
    })

    readonly property var edgeKind: ({
        Chain: 0, TransformToContext: 1, AudioToController: 2, ControllerToMaster: 3,
        LegacyClipToTransform: 4, ClipToShaderAudio: 5, ScriptToData: 6, InputToMaster: 7,
        AbToOutput: 8, StreamToMixer: 9, MixerToOutput: 10, AudioEffectChain: 11,
        ClipToAudioScript: 12
    })

    // Domain hues. These carry meaning (video / A-B / raw audio / mixed audio / script data) and
    // have no counterpart in the theme tokens, so they stay fixed; the A/B hue follows the theme.
    readonly property color chainHue: "#50a8d8"
    readonly property color abHue: Theme.destructive
    readonly property color streamHue: "#e4b042"
    readonly property color mixedHue: "#e88030"
    readonly property color scriptHue: "#70c0a8"

    function desat(c, keep) {
        return Qt.hsva(c.hsvHue < 0 ? 0 : c.hsvHue, c.hsvSaturation * keep, c.hsvValue, c.a)
    }

    function portColor(kind) {
        const k = portKind
        switch (kind) {
        case k.ChainIn:
        case k.ChainOut:
            return desat(chainHue, 0.8)
        case k.AbIn:
        case k.AbOut:
            return desat(abHue, 0.8)
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
            return desat(scriptHue, 0.8)
        }
        return Theme.mutedForeground
    }

    function portKindName(kind) {
        const k = portKind
        switch (kind) {
        case k.ChainIn: case k.ChainOut: return qsTr("Video")
        case k.AbIn: case k.AbOut: return qsTr("A/B deck")
        case k.AudioControllerOut: case k.MasterAudioInputOut: return qsTr("Audio stream")
        case k.AudioMixerIn: return qsTr("Mixer input")
        case k.AudioMixerOut: return qsTr("Mixed audio")
        case k.AudioEffectIn: case k.AudioEffectOut: return qsTr("Audio effect")
        case k.MasterAudioIn: return qsTr("Audio output")
        case k.AudioIn: case k.ShaderAudioIn: case k.AudioOut: return qsTr("Audio")
        case k.ScriptOut: case k.DataIn: return qsTr("Script data")
        }
        return ""
    }

    function edgeColor(kind) {
        const e = edgeKind
        switch (kind) {
        case e.Chain: return desat("#70b8ff", 0.8)
        case e.AbToOutput: return desat(abHue, 0.8)
        case e.AudioToController: return desat(mixedHue, 0.8)
        case e.ClipToShaderAudio: return desat("#e89040", 0.8)
        case e.ClipToAudioScript:
        case e.StreamToMixer:
        case e.AudioEffectChain:
            return desat(streamHue, 0.8)
        case e.MixerToOutput:
        case e.InputToMaster:
        case e.ControllerToMaster:
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
          inKinds: [], outKinds: [1, 7] },
        { name: qsTr("Process"), icon: "wand-sparkles", hue: "#78aad2", header: 20, category: "video",
          inKinds: [0], outKinds: [1] },
        { name: qsTr("Audio Effect"), icon: "audio-waveform", hue: "#e4b042", header: 20, category: "audio",
          inKinds: [14], outKinds: [15] },
        { name: qsTr("Layer"), icon: "layers", hue: "#78aad2", header: 20, category: "video",
          inKinds: [0], outKinds: [1] },
        { name: qsTr("A/B Select"), icon: "split", hue: "#e67878", header: 20, category: "video",
          inKinds: [0, 11], outKinds: [2] },
        { name: qsTr("Output"), icon: "monitor", hue: "#96c8f0", header: 20, category: "video",
          inKinds: [0, 3], outKinds: [] },
        { name: qsTr("Audio Mixer"), icon: "sliders-vertical", hue: "#e4b042", header: 20, category: "audio",
          inKinds: [12], outKinds: [13] },
        { name: qsTr("Audio Output"), icon: "speaker", hue: "#e88030", header: 22, category: "audio",
          inKinds: [8], outKinds: [] },
        { name: qsTr("Mic Input"), icon: "mic", hue: "#e4b042", header: 20, category: "audio",
          inKinds: [], outKinds: [9] },
        { name: qsTr("Audio Capture"), icon: "audio-lines", hue: "#e4b042", header: 20, category: "audio",
          inKinds: [], outKinds: [9] },
        { name: qsTr("Lua Script"), icon: "binary", hue: "#78dcaa", header: 20, category: "script",
          inKinds: [11], outKinds: [10] },
        { name: qsTr("Audio Script"), icon: "activity", hue: "#e6be5a", header: 20, category: "script",
          inKinds: [6], outKinds: [10] },
        { name: qsTr("Trigger"), icon: "clock", hue: "#78bee6", header: 20, category: "script",
          inKinds: [], outKinds: [10] }
    ]

    // Default node width per type, used to place a node so its port lands under the cursor.
    readonly property var defaultWidth: [122, 116, 116, 158, 168, 116, 158, 168, 100, 100, 100, 100, 100]

    function tooltip(type) {
        switch (type) {
        case nodeType.Layer:
            return qsTr("Double-click a layer name to rename. The eye toggles visibility; the arrows reorder.")
        case nodeType.AbSelect:
            return qsTr("Double-click an input name to rename. A and B send the input to that deck. "
                        + "Wire a Script node's output here to automate deck assignment.")
        case nodeType.AudioMixer:
            return qsTr("Double-click a name to rename. Edit opens per-input volume and mute.")
        }
        return ""
    }

    // Name of the editor the shell should open for a node, "" if it has none.
    function editorKind(type, hasEditor) {
        switch (type) {
        case nodeType.Input: return "source"
        case nodeType.Process: return hasEditor ? "process" : ""
        case nodeType.AudioEffect: return hasEditor ? "audioEffect" : ""
        case nodeType.Layer: return "layerLayout"
        case nodeType.AudioMixer: return "mixer"
        case nodeType.MasterAudioInput: return "mic"
        case nodeType.MasterAudioCapture: return "capture"
        case nodeType.Script: return "script"
        case nodeType.AudioScript: return "audioScript"
        case nodeType.Trigger: return "trigger"
        }
        return ""
    }
}

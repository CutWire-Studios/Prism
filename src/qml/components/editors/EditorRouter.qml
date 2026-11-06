import QtQuick
import Prism

// Opens the node editor for openEditorRequested(nodeId, kind). Each editor is a separate file
// in this directory, created on first use and reused; it exposes openFor(nodeId, kind).
QtObject {
    id: root

    property Item host

    readonly property var files: ({
        "source": "SourceEditor.qml",
        "process": "ProcessEditor.qml",
        "audioEffect": "AudioEffectEditor.qml",
        "audio": "NodeAudioEditor.qml",
        "layerLayout": "LayerLayoutEditor.qml",
        "layerCanvas": "LayerCanvasEditor.qml",
        "mixer": "MixerEditor.qml",
        "mic": "AudioDeviceEditor.qml",
        "capture": "AudioDeviceEditor.qml",
        "script": "ScriptEditor.qml",
        "audioScript": "AudioScriptEditor.qml",
        "trigger": "TriggerEditor.qml"
    })

    property var _instances: ({})

    function open(nodeId, kind) {
        const file = files[kind]
        if (!file) {
            console.warn("EditorRouter: no editor for kind", kind)
            return
        }
        let dlg = _instances[file]
        if (!dlg) {
            const c = Qt.createComponent(Qt.resolvedUrl(file))
            if (c.status !== Component.Ready) {
                console.warn("EditorRouter:", file, c.errorString())
                return
            }
            dlg = c.createObject(host)
            _instances = Object.assign({}, _instances, { [file]: dlg })
        }
        dlg.openFor(nodeId, kind)
    }
}

import QtQuick
import Prism

// Opens the app-level dialogs AppController asks for. Place once in the shell:
// `AppDialogs { anchors.fill: parent }`.
Item {
    id: root

    function _openAddSource(kind) {
        switch (kind) {
        case "webrtc": webrtc.ensure().open(); break
        case "url": urlDlg.ensure().open(); break
        default: SourcePrompts.addDefault(kind)
        }
    }

    Connections {
        target: AppController

        function onAppDialogRequested(id) {
            if (id.indexOf("addSource:") === 0) {
                root._openAddSource(id.substring(10))
                return
            }
            switch (id) {
            case "hotkeys": hotkeys.ensure().open(); break
            case "recording": recording.ensure().open(); break
            case "remote": remote.ensure().open(); break
            case "osc": osc.ensure().open(); break
            case "agent": agent.ensure().open(); break
            case "obs": obs.ensure().open(); break
            }
        }

        function onSourceEditorRequested(kind) {
            switch (kind) {
            case "shader": shaderEditor.ensure().openForNew(); break
            case "svgTemplate": svgEditor.ensure().openForNew(); break
            case "text": textEditor.ensure().openForNew(); break
            case "shape": shapeEditor.ensure().openForNew(); break
            }
        }

        function onRecoveryRequested(autosavePath, backupPaths) {
            recovery.ensure().show(autosavePath, backupPaths)
        }
    }

    LazyLoader { id: hotkeys; sourceComponent: HotkeyEditorDialog { } }
    LazyLoader { id: recording; sourceComponent: RecordingPanelDialog { } }
    LazyLoader { id: remote; sourceComponent: RemoteServerDialog { } }
    LazyLoader { id: osc; sourceComponent: OscSettingsDialog { } }
    LazyLoader { id: agent; sourceComponent: AgentAccessDialog { } }
    LazyLoader { id: obs; sourceComponent: ObsConnectDialog { } }
    LazyLoader { id: recovery; keepLoaded: true; sourceComponent: SessionRecoveryDialog { } }
    LazyLoader { id: webrtc; sourceComponent: WebRtcSourceDialog { } }
    LazyLoader { id: urlDlg; sourceComponent: AddUrlDialog { } }
    LazyLoader { id: shaderEditor; sourceComponent: ShaderSourceEditor { } }
    LazyLoader { id: svgEditor; sourceComponent: SvgTemplateEditor { } }
    LazyLoader { id: textEditor; sourceComponent: TextSourceEditor { } }
    LazyLoader { id: shapeEditor; sourceComponent: ShapeSourceEditor { } }
}

import QtQuick
import Prism

// Every way to add an Input (or audio source) node. Media, URL and phone camera go through
// AppController's file / app-dialog requests; camera, screen, slideshow, canvas and NDI are added
// with defaults and configured in the inspector; mic and audio capture become plain nodes placed
// at worldX/worldY (NaN = auto placement).
ThemedContextMenu {
    id: root

    property real worldX: NaN
    property real worldY: NaN

    function addAudioNode(type) {
        AppController.graph.addNode(type, root.worldX, root.worldY)
    }

    ThemedMenuItem {
        text: qsTr("Media File…")
        icon.name: "film"
        onTriggered: AppController.requestAddFiles()
    }
    ThemedMenuItem {
        text: qsTr("Video URL…")
        icon.name: "link"
        onTriggered: AppController.requestAddSource("url")
    }
    ThemedMenuItem {
        text: qsTr("Slideshow")
        icon.name: "folder"
        onTriggered: AppController.requestAddSource("slideshow")
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Camera")
        icon.name: "camera"
        onTriggered: AppController.requestAddSource("camera")
    }
    ThemedMenuItem {
        text: qsTr("Mic Input")
        icon.name: "mic"
        onTriggered: root.addAudioNode(NodePalette.nodeType.MasterAudioInput)
    }
    ThemedMenuItem {
        text: qsTr("Audio Capture")
        icon.name: "audio-lines"
        onTriggered: root.addAudioNode(NodePalette.nodeType.MasterAudioCapture)
    }
    ThemedMenuItem {
        text: qsTr("Screen Capture")
        icon.name: "screen-share"
        onTriggered: AppController.requestAddSource("screen")
    }
    ThemedMenuItem {
        text: qsTr("Window Capture")
        icon.name: "app-window"
        onTriggered: AppController.requestAddSource("window")
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Canvas")
        icon.name: "square-dashed"
        onTriggered: AppController.requestAddSource("canvas")
    }
    ThemedMenuItem {
        text: qsTr("Shader…")
        icon.name: "sparkles"
        onTriggered: AppController.requestAddSource("shader")
    }
    ThemedMenuItem {
        text: qsTr("SVG Template…")
        icon.name: "layout-grid"
        onTriggered: AppController.requestAddSource("svgTemplate")
    }
    ThemedMenuItem {
        text: qsTr("Text…")
        icon.name: "type"
        onTriggered: AppController.requestAddSource("text")
    }
    ThemedMenuItem {
        text: qsTr("Shape…")
        icon.name: "shapes"
        onTriggered: AppController.requestAddSource("shape")
    }
    ThemedMenuItem {
        text: qsTr("NDI Source")
        icon.name: "radio-receiver"
        enabled: SourcePrompts.ndiAvailable
        onTriggered: AppController.requestAddSource("ndi")
    }
    ThemedMenuItem {
        text: qsTr("Phone Camera (WebRTC)…")
        icon.name: "smartphone"
        enabled: SourcePrompts.webrtcAvailable
        onTriggered: AppController.requestAddSource("webrtc")
    }
}

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Prism

// Properties of an Input node, by source kind. Text, shape, SVG and shader sources are edited in
// their own dialogs (see InspectorPanel).
InspectorForm {
    id: root

    readonly property var source: params.source ? params.source : ({})
    readonly property var settings: params.settings ? params.settings : ({})
    readonly property int kind: source.kind
    readonly property bool portalCapture: ClipEditTools.captureSupported()
    readonly property bool trimmable: kind === 0 || kind === 12
    readonly property int deck: AppController.activeNodeLive === Number(nodeId) ? 0
                              : AppController.activeNodeStaging === Number(nodeId) ? 1 : -1

    property var cameras: []
    property var ndiSources: []
    property var captureTargets: []
    property real duration: 0
    readonly property real startTime: settings.startTime || 0
    readonly property real endTime: settings.endTime === undefined || settings.endTime < 0
                                    || settings.endTime > duration ? duration : settings.endTime
    readonly property real playhead: deck === 0 ? AppController.deckTimeLive : AppController.deckTimeStaging

    function setSource(patch) {
        set({ source: Object.assign({}, source, patch) })
    }

    function setSettings(patch) {
        set({ settings: Object.assign({}, settings, patch) })
    }

    function refreshNdi() {
        if (ClipEditTools.ndiAvailable())
            ndiSources = ClipEditTools.ndiSources()
    }

    function refreshCaptureTargets() {
        captureTargets = kind === 6 ? SourcePrompts.windowLabels() : SourcePrompts.screenLabels()
    }

    function formatTime(secs) {
        const s0 = Math.max(0, secs)
        const m = Math.floor(s0 / 60)
        const s = s0 - m * 60
        return m + ":" + (s < 10 ? "0" : "") + s.toFixed(2)
    }

    Component.onCompleted: {
        switch (kind) {
        case 0:
        case 12:
            duration = ClipEditTools.mediaDuration(source.path)
            break
        case 3:
            cameras = ClipEditTools.cameraDevices()
            break
        case 4:
        case 6:
            if (!portalCapture)
                refreshCaptureTargets()
            break
        case 9:
            ndiSources = source.path ? [source.path] : []
            Qt.callLater(refreshNdi)
            break
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Select Image Folder")
        onAccepted: root.setSource({ path: AppController.localPath(selectedFolder) })
    }

    FileDialog {
        id: imageDialog
        title: qsTr("Replace Image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.webp)")]
        onAccepted: {
            const path = AppController.localPath(selectedFile)
            root.setSource({ path: path, displayName: path.split("/").pop().replace(/\.[^.]*$/, "") })
        }
    }

    ThemedLabel {
        Layout.fillWidth: true
        visible: !!root.source.path && root.kind !== 3 && root.kind !== 9 && root.kind !== 10
        text: root.source.path || ""
        elide: Text.ElideMiddle
        wrapMode: Text.NoWrap
    }

    ThemedButton {
        Layout.fillWidth: true
        visible: root.kind === 1
        text: qsTr("Replace image…")
        onClicked: imageDialog.open()
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 3
        label: qsTr("Camera")
        ThemedComboBox {
            Layout.fillWidth: true
            textRole: "label"
            model: root.cameras
            currentIndex: {
                for (let i = 0; i < root.cameras.length; ++i) {
                    if ((root.source.path || "").length > 0 ? root.cameras[i].id === root.source.path
                                                            : root.cameras[i].isDefault)
                        return i
                }
                return 0
            }
            onActivated: index => {
                const cam = root.cameras[index]
                root.setSource({
                    path: cam.id,
                    cameraIndex: cam.isDefault ? 0 : cam.index,
                    displayName: cam.isDefault ? qsTr("Default Camera") : cam.label.split("  [")[0].trim() || cam.id
                })
            }
        }
    }

    ThemedLabel {
        Layout.fillWidth: true
        visible: root.kind === 3 && root.cameras.length <= 1
        text: qsTr("No camera devices were found.")
    }

    ThemedLabel {
        Layout.fillWidth: true
        visible: root.kind === 9 && !ClipEditTools.ndiAvailable()
        text: qsTr("NDI is not available in this build.")
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 9 && ClipEditTools.ndiAvailable()
        label: qsTr("NDI source")
        ThemedComboBox {
            Layout.fillWidth: true
            model: root.ndiSources
            currentIndex: Math.max(0, root.ndiSources.indexOf(root.source.path))
            onActivated: index => root.setSource({ path: root.ndiSources[index], displayName: root.ndiSources[index] })
        }
        IconButton {
            glyph: "refresh-cw"
            tooltip: qsTr("Refresh")
            onClicked: root.refreshNdi()
        }
    }

    ThemedLabel {
        Layout.fillWidth: true
        visible: root.kind === 9 && ClipEditTools.ndiAvailable() && root.ndiSources.length === 0
        text: qsTr("No NDI sources found on the network.")
    }

    ThemedButton {
        Layout.fillWidth: true
        visible: (root.kind === 4 || root.kind === 6) && root.portalCapture
        text: qsTr("Choose what to capture…")
        onClicked: ClipEditTools.pickCapture(root.nodeId)
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: (root.kind === 4 || root.kind === 6) && !root.portalCapture
        label: root.kind === 6 ? qsTr("Window") : qsTr("Screen")
        ThemedComboBox {
            Layout.fillWidth: true
            model: root.captureTargets
            currentIndex: Math.max(0, Math.min((root.kind === 6 ? root.source.windowIndex : root.source.screenIndex) || 0,
                                               root.captureTargets.length - 1))
            onActivated: index => root.setSource(root.kind === 6
                ? { windowIndex: index, displayName: root.captureTargets[index] }
                : { screenIndex: index, displayName: qsTr("Screen %1").arg(index + 1) })
        }
        IconButton {
            visible: root.kind === 6
            glyph: "refresh-cw"
            tooltip: qsTr("Refresh window list")
            onClicked: root.refreshCaptureTargets()
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 5
        label: qsTr("Width")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 16
            to: 16384
            unit: "px"
            value: root.source.canvasWidth || 1280
            onEdited: v => {
                root.setSource({ canvasWidth: Math.round(v) })
                ClipEditTools.refreshThumbnail(root.nodeId)
            }
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 5
        label: qsTr("Height")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 16
            to: 16384
            unit: "px"
            value: root.source.canvasHeight || 720
            onEdited: v => {
                root.setSource({ canvasHeight: Math.round(v) })
                ClipEditTools.refreshThumbnail(root.nodeId)
            }
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 5
        label: qsTr("Fill")
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("Checkered"), qsTr("Transparent"), qsTr("Color")]
            currentIndex: root.source.canvasFill || 0
            onActivated: index => {
                root.setSource({ canvasFill: index })
                ClipEditTools.refreshThumbnail(root.nodeId)
            }
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 5 && root.source.canvasFill === 2
        label: qsTr("Color")
        StyleColorButton {
            swatch: root.source.color || "#ffffffff"
            onPicked: c => {
                root.setSource({ color: String(c) })
                ClipEditTools.refreshThumbnail(root.nodeId)
            }
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 2
        label: qsTr("Folder")
        ThemedTextField {
            Layout.fillWidth: true
            text: root.source.path || ""
            onEditingFinished: if (text !== root.source.path) root.setSource({ path: text })
        }
        ThemedButton {
            text: qsTr("Browse…")
            onClicked: folderDialog.open()
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 2
        label: qsTr("Per slide")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 1
            to: 3600
            unit: "s"
            value: Math.round((root.source.slideshowIntervalMs || 3000) / 1000)
            onEdited: v => root.setSource({ slideshowIntervalMs: Math.round(v) * 1000 })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 2
        label: qsTr("Effect")
        ThemedComboBox {
            Layout.fillWidth: true
            model: ClipEditTools.slideshowEffects()
            currentIndex: root.source.slideshowEffect || 0
            onActivated: index => root.setSource({ slideshowEffect: index })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 2
        label: qsTr("Transition")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 100
            to: 10000
            step: 100
            unit: "ms"
            value: root.source.slideshowTransitionMs || 800
            onEdited: v => root.setSource({ slideshowTransitionMs: Math.round(v) })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.trimmable && root.duration > 0
        label: qsTr("In")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 0
            to: Math.max(0, root.endTime - 0.1)
            step: 0.1
            decimals: 2
            unit: "s"
            value: root.startTime
            onEdited: v => root.setSettings({ startTime: v })
        }
        ThemedButton {
            enabled: root.deck >= 0
            text: qsTr("Playhead")
            tooltip: qsTr("Set from the playhead of the deck this clip is loaded on")
            onClicked: root.setSettings({ startTime: Math.min(root.playhead, root.endTime - 0.1) })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.trimmable && root.duration > 0
        label: qsTr("Out")
        ThemedNumberField {
            Layout.fillWidth: true
            from: root.startTime + 0.1
            to: root.duration
            step: 0.1
            decimals: 2
            unit: "s"
            value: root.endTime
            onEdited: v => root.setSettings({ endTime: v })
        }
        ThemedButton {
            enabled: root.deck >= 0
            text: qsTr("Playhead")
            tooltip: qsTr("Set from the playhead of the deck this clip is loaded on")
            onClicked: root.setSettings({ endTime: Math.max(root.startTime + 0.1, Math.min(root.duration, root.playhead)) })
        }
    }

    ThemedLabel {
        Layout.fillWidth: true
        visible: root.trimmable && root.duration > 0
        text: qsTr("Length %1 of %2").arg(root.formatTime(root.endTime - root.startTime)).arg(root.formatTime(root.duration))
    }

    ThemedSwitch {
        visible: !!root.params.repeatable
        text: qsTr("Loop")
        checked: !!root.params.repeat
        onToggled: root.set({ repeat: checked })
    }

    StyleSliderRow {
        Layout.fillWidth: true
        visible: !!root.params.speedControl && root.deck >= 0
        label: qsTr("Speed")
        from: 0.25
        to: 4
        step: 0.05
        decimals: 2
        value: root.deck === 0 ? AppController.deckSpeedLive : AppController.deckSpeedStaging
        onEdited: v => AppController.setDeckSpeedValue(root.deck, v)
    }

    ThemedLabel {
        Layout.fillWidth: true
        visible: root.kind === 10
        wrapMode: Text.WordWrap
        text: qsTr("Phone camera session. Add another Phone Camera to pair a different phone.")
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: !!root.params.hasAudio
        label: qsTr("Audio")
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("Always play"), qsTr("Only when on air")]
            currentIndex: root.params.audioPlaybackMode === 1 ? 1 : 0
            onActivated: index => root.set({ audioPlaybackMode: index })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: !!root.params.hasAudio
        label: qsTr("Delay")
        ThemedNumberField {
            Layout.fillWidth: true
            from: -5000
            to: 5000
            step: 10
            unit: "ms"
            value: root.params.audioDelayMs || 0
            onEdited: v => root.set({ audioDelayMs: Math.round(v) })
        }
    }
}

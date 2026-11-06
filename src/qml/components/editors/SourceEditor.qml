pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Prism

// Edit dialog of an Input node. Text, shape, SVG template and shader sources have editors of
// their own and are handed over; every other kind is edited here and written back as one undo
// step when accepted.
ThemedDialog {
    id: root

    readonly property var delegates: ({ 7: "ShaderSourceEditor.qml", 11: "TextSourceEditor.qml",
                                        13: "ShapeSourceEditor.qml", 14: "SvgTemplateEditor.qml" })

    property var nodeId: 0
    property string mode: ""
    property var source: ({})
    property var settings: ({})
    property bool repeat: false
    property var _editors: ({})

    // trim
    property real duration: 1
    property real playhead: 0
    property real startTime: 0
    property real endTime: 1
    property bool playing: false
    property string status: ""

    // slideshow
    property string folder: ""
    property int intervalSec: 3
    property int effect: 0
    property int transitionMs: 800

    // camera / ndi
    property var cameras: []
    property int cameraIndex: 0
    property var ndiSources: []
    property int ndiIndex: 0

    // screen / window capture where the system picker is not used
    property var captureTargets: []
    property int captureIndex: 0
    readonly property bool captureIsWindow: source.kind === 6
    readonly property bool portalCapture: ClipEditTools.captureSupported()

    // canvas
    property int canvasWidth: 1280
    property int canvasHeight: 720
    property int canvasFill: 0
    property color canvasColor: "#ffffffff"

    readonly property string fileName: source.path ? String(source.path).split("/").pop() : ""
    readonly property bool valid: mode === "camera" ? cameras.length > 1
                                : mode === "ndi" ? ndiSources.length > 0
                                : mode === "screen" && !portalCapture ? captureTargets.length > 0 : true

    title: {
        switch (mode) {
        case "trim": return qsTr("Edit Clip — %1").arg(fileName)
        case "slideshow": return qsTr("Edit Slideshow")
        case "camera": return qsTr("Select Camera")
        case "screen": return source.kind === 6 ? qsTr("Capture Window") : qsTr("Capture Screen")
        case "canvas": return qsTr("Edit Canvas")
        case "ndi": return qsTr("Select NDI Source")
        case "audio": return qsTr("Edit — %1").arg(fileName)
        }
        return qsTr("Edit Source")
    }
    preferredWidth: mode === "trim" ? 760 : Theme.dialogWidthMd
    showAccept: (mode !== "screen" || !portalCapture) && valid
    rejectText: (mode === "screen" && portalCapture) || !valid ? qsTr("Close") : qsTr("Cancel")
    acceptOnReturn: false

    function openFor(id, kind) {
        const p = ClipEditTools.nodeParams(id)
        const src = p.source || {}
        const file = delegates[src.kind]
        if (file !== undefined) {
            _delegate(file, id)
            return
        }
        const m = _modeFor(src.kind)
        if (m.length === 0)
            return

        nodeId = id
        source = src
        settings = p.settings || {}
        repeat = p.repeat === true
        status = ""
        playing = false
        mode = m

        switch (m) {
        case "trim": {
            const d = ClipEditTools.mediaDuration(src.path)
            duration = d > 0 ? d : 1
            const end = settings.endTime === undefined ? -1 : settings.endTime
            endTime = end < 0 || end > duration ? duration : end
            startTime = Math.min(settings.startTime || 0, duration)
            playhead = startTime
            break
        }
        case "slideshow":
            folder = src.path || ""
            intervalSec = Math.max(1, Math.round((src.slideshowIntervalMs || 3000) / 1000))
            effect = src.slideshowEffect || 0
            transitionMs = src.slideshowTransitionMs || 800
            break
        case "camera": {
            cameras = ClipEditTools.cameraDevices()
            let idx = 0
            for (let i = 0; i < cameras.length; ++i) {
                if ((src.path || "").length > 0 ? cameras[i].id === src.path : i === (src.cameraIndex || 0))
                    idx = i
            }
            cameraIndex = idx
            break
        }
        case "canvas":
            canvasWidth = src.canvasWidth || 1280
            canvasHeight = src.canvasHeight || 720
            canvasFill = src.canvasFill || 0
            canvasColor = src.color || "#ffffffff"
            break
        case "ndi":
            ndiSources = []
            ndiIndex = 0
            Qt.callLater(refreshNdi)
            break
        case "screen":
            if (!portalCapture)
                refreshCaptureTargets()
            break
        }
        open()
    }

    function _modeFor(kind) {
        switch (kind) {
        case 0: return "trim"
        case 2: return "slideshow"
        case 3: return "camera"
        case 4:
        case 6: return "screen"
        case 5: return "canvas"
        case 9: return "ndi"
        case 12: return "audio"
        }
        return ""
    }

    function _delegate(file, id) {
        let ed = _editors[file]
        if (!ed) {
            const c = Qt.createComponent(Qt.resolvedUrl(file))
            if (c.status !== Component.Ready) {
                console.warn("SourceEditor:", file, c.errorString())
                return
            }
            ed = c.createObject(root)
            _editors = Object.assign({}, _editors, { [file]: ed })
        }
        ed.openFor(id)
    }

    function refreshNdi() {
        if (!ClipEditTools.ndiAvailable())
            return
        const keep = ndiIndex < ndiSources.length ? ndiSources[ndiIndex] : source.path
        ndiSources = ClipEditTools.ndiSources()
        const idx = ndiSources.indexOf(keep)
        ndiIndex = idx >= 0 ? idx : 0
    }

    function refreshCaptureTargets() {
        captureTargets = captureIsWindow ? SourcePrompts.windowLabels() : SourcePrompts.screenLabels()
        const current = captureIsWindow ? source.windowIndex : source.screenIndex
        captureIndex = Math.max(0, Math.min(current || 0, captureTargets.length - 1))
    }

    function clampTime(t) {
        return Math.min(duration, Math.max(0, t))
    }

    function formatTime(secs) {
        const s0 = Math.max(0, secs)
        const m = Math.floor(s0 / 60)
        const s = s0 - m * 60
        return m + ":" + (s < 10 ? "0" : "") + s.toFixed(2)
    }

    function parseTime(text) {
        const s = text.trim()
        let result = NaN
        if (s.indexOf(":") >= 0) {
            const parts = s.split(":")
            if (parts.length !== 2)
                return NaN
            const m = parseInt(parts[0])
            const sec = parseFloat(parts[1])
            if (isNaN(m) || isNaN(sec) || m < 0 || sec < 0 || sec >= 60)
                return NaN
            result = m * 60 + sec
        } else {
            result = parseFloat(s)
            if (isNaN(result) || result < 0)
                return NaN
        }
        return result
    }

    function seekTo(t) {
        playhead = clampTime(t)
    }

    function setStart() {
        if (playhead >= endTime - 0.1) {
            status = qsTr("Start must be at least 0.1 s before end (%1)").arg(formatTime(endTime))
            return
        }
        startTime = playhead
        status = ""
    }

    function setEnd() {
        if (playhead <= startTime + 0.1) {
            status = qsTr("End must be at least 0.1 s after start (%1)").arg(formatTime(startTime))
            return
        }
        if (playhead > duration) {
            status = qsTr("End cannot exceed clip duration (%1)").arg(formatTime(duration))
            return
        }
        endTime = playhead
        status = ""
    }

    function canvasLabel() {
        const fill = canvasFill === 1 ? qsTr("Transparent")
                   : canvasFill === 2 ? String(canvasColor).toUpperCase() : qsTr("Checkered")
        let a = canvasWidth, b = canvasHeight
        while (b !== 0) {
            const t = a % b
            a = b
            b = t
        }
        return qsTr("Canvas %1:%2 (%3)").arg(canvasWidth / a).arg(canvasHeight / a).arg(fill)
    }

    onAccepted: {
        let upd = {}
        switch (mode) {
        case "trim":
            upd = { settings: Object.assign({}, settings, { startTime: startTime, endTime: endTime }), repeat: repeat }
            break
        case "audio":
            upd = { repeat: repeat }
            break
        case "slideshow":
            upd = { source: Object.assign({}, source, { path: folder, slideshowIntervalMs: intervalSec * 1000,
                                                        slideshowEffect: effect, slideshowTransitionMs: transitionMs }),
                    repeat: repeat }
            break
        case "camera": {
            const cam = cameras[cameraIndex]
            upd = { source: Object.assign({}, source, {
                path: cam.id,
                cameraIndex: cam.isDefault ? 0 : cam.index,
                displayName: cam.isDefault ? qsTr("Default Camera") : cam.label.split("  [")[0].trim() || cam.id
            }) }
            break
        }
        case "canvas":
            upd = { source: Object.assign({}, source, {
                canvasWidth: canvasWidth,
                canvasHeight: canvasHeight,
                canvasFill: canvasFill,
                color: canvasFill === 2 ? String(canvasColor) : source.color,
                displayName: canvasLabel()
            }) }
            break
        case "ndi": {
            const name = ndiSources[ndiIndex]
            upd = { source: Object.assign({}, source, { path: name, displayName: name }) }
            break
        }
        case "screen":
            if (portalCapture)
                return
            upd = { source: Object.assign({}, source, captureIsWindow
                ? { windowIndex: captureIndex, displayName: captureTargets[captureIndex] }
                : { screenIndex: captureIndex, displayName: qsTr("Screen %1").arg(captureIndex + 1) }) }
            break
        default:
            return
        }
        AppController.graph.setParams(nodeId, upd)
        if (mode === "canvas")
            ClipEditTools.refreshThumbnail(nodeId)
    }

    onClosed: playing = false

    Timer {
        interval: 250
        repeat: true
        running: root.playing && root.opened
        onTriggered: {
            if (root.playhead + 0.25 >= root.endTime) {
                root.playing = false
                root.playhead = root.endTime
            } else {
                root.playhead += 0.25
            }
        }
    }

    Connections {
        target: ClipEditTools
        function onCaptureFinished(id, ok) {
            if (id === root.nodeId && root.mode === "screen")
                root.close()
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Select Image Folder")
        onAccepted: root.folder = AppController.localPath(selectedFolder)
    }

    ThemedColorDialog {
        id: canvasColorDialog
        onAccepted: root.canvasColor = selectedColor
    }

    contentItem: Loader {
        sourceComponent: {
            switch (root.mode) {
            case "trim": return trimPane
            case "slideshow": return slideshowPane
            case "camera": return cameraPane
            case "screen": return screenPane
            case "canvas": return canvasPane
            case "ndi": return ndiPane
            case "audio": return audioPane
            }
            return null
        }
    }

    Component {
        id: audioPane
        ColumnLayout {
            ThemedCheckBox {
                text: qsTr("Loop")
                checked: root.repeat
                onToggled: root.repeat = checked
            }
        }
    }

    Component {
        id: trimPane
        ColumnLayout {
            spacing: Theme.spacingLg

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 320
                color: Theme.previewLetterbox

                ClipFrameView {
                    id: view
                    readonly property real ar: imageAspect > 0 ? imageAspect : 16 / 9
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height * ar)
                    height: width / ar
                    path: root.source.path || ""
                    time: root.playhead
                    maxSize: 960
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMd

                IconButton {
                    glyph: root.playing ? "pause" : "play"
                    tooltip: root.playing ? qsTr("Pause") : qsTr("Play")
                    onClicked: {
                        if (!root.playing && root.playhead >= root.endTime - 0.01)
                            root.playhead = root.startTime
                        root.playing = !root.playing
                    }
                }
                ThemedSlider {
                    Layout.fillWidth: true
                    label: qsTr("Position")
                    showValueTooltip: false
                    from: 0
                    to: root.duration
                    value: root.playhead
                    onMoved: root.seekTo(value)
                }
                ThemedTextField {
                    id: stamp
                    Layout.preferredWidth: 84
                    horizontalAlignment: Text.AlignHCenter
                    onEditingFinished: {
                        const t = root.parseTime(text)
                        if (isNaN(t)) {
                            root.status = qsTr("Invalid time format — use M:SS.ss")
                        } else {
                            root.status = ""
                            root.seekTo(t)
                        }
                        text = root.formatTime(root.playhead)
                    }
                    Binding on text {
                        when: !stamp.activeFocus
                        value: root.formatTime(root.playhead)
                    }
                }
                ThemedLabel { text: "/ " + root.formatTime(root.duration) }
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.spacingSm

                Repeater {
                    model: [-10, -5, -1, -0.1, 0.1, 1, 5, 10]

                    ThemedButton {
                        required property real modelData
                        text: (modelData > 0 ? "+" : "−") + Math.abs(modelData) + "s"
                        onClicked: {
                            root.playing = false
                            root.seekTo(root.playhead + modelData)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingLg

                ThemedButton {
                    text: qsTr("Set start")
                    glyph: "arrow-left-to-line"
                    onClicked: root.setStart()
                }
                ThemedLabel {
                    tone: "default"
                    text: root.formatTime(root.startTime)
                }
                Item { Layout.fillWidth: true }
                ThemedLabel {
                    text: qsTr("Length %1").arg(root.formatTime(root.endTime - root.startTime))
                }
                Item { Layout.fillWidth: true }
                ThemedLabel {
                    tone: "default"
                    text: root.formatTime(root.endTime)
                }
                ThemedButton {
                    text: qsTr("Set end")
                    glyph: "arrow-right-to-line"
                    onClicked: root.setEnd()
                }
            }

            ThemedLabel {
                Layout.fillWidth: true
                visible: root.status.length > 0
                color: Theme.destructive
                text: root.status
            }

            ThemedCheckBox {
                text: qsTr("Loop")
                checked: root.repeat
                onToggled: root.repeat = checked
            }
        }
    }

    Component {
        id: slideshowPane
        GridLayout {
            columns: 2
            columnSpacing: Theme.spacingLg
            rowSpacing: Theme.spacingLg

            ThemedLabel { text: qsTr("Folder") }
            RowLayout {
                Layout.fillWidth: true
                ThemedTextField {
                    Layout.fillWidth: true
                    text: root.folder
                    onTextEdited: root.folder = text
                }
                ThemedButton {
                    text: qsTr("Browse…")
                    onClicked: folderDialog.open()
                }
            }

            ThemedLabel { text: qsTr("Seconds per slide") }
            ThemedNumberField {
                Layout.fillWidth: true
                from: 1
                to: 3600
                unit: "s"
                value: root.intervalSec
                onEdited: v => root.intervalSec = Math.round(v)
            }

            ThemedLabel { text: qsTr("Transition effect") }
            ThemedComboBox {
                Layout.fillWidth: true
                model: ClipEditTools.slideshowEffects()
                currentIndex: root.effect
                onActivated: index => root.effect = index
            }

            ThemedLabel { text: qsTr("Transition duration") }
            ThemedNumberField {
                Layout.fillWidth: true
                from: 100
                to: 10000
                step: 100
                unit: "ms"
                value: root.transitionMs
                onEdited: v => root.transitionMs = Math.round(v)
            }

            Item { width: 1; height: 1 }
            ThemedCheckBox {
                text: qsTr("Loop")
                checked: root.repeat
                onToggled: root.repeat = checked
            }
        }
    }

    Component {
        id: cameraPane
        ColumnLayout {
            spacing: Theme.spacingLg

            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                text: root.cameras.length > 1 ? qsTr("Choose camera device:") : qsTr("No camera devices were found.")
            }
            ThemedComboBox {
                Layout.fillWidth: true
                visible: root.cameras.length > 1
                textRole: "label"
                model: root.cameras
                currentIndex: root.cameraIndex
                onActivated: index => root.cameraIndex = index
            }
        }
    }

    Component {
        id: screenPane
        ColumnLayout {
            spacing: Theme.spacingLg

            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                text: root.portalCapture
                      ? qsTr("The system picker chooses what is captured. Picking again replaces the current selection.")
                      : root.captureTargets.length > 0
                        ? (root.captureIsWindow ? qsTr("Choose the window to capture:") : qsTr("Choose the screen to capture:"))
                        : qsTr("No screens or capturable windows were found.")
            }
            ThemedComboBox {
                Layout.fillWidth: true
                visible: !root.portalCapture && root.captureTargets.length > 0
                model: root.captureTargets
                currentIndex: root.captureIndex
                onActivated: index => root.captureIndex = index
            }
            ThemedButton {
                visible: !root.portalCapture && root.captureIsWindow
                text: qsTr("Refresh window list")
                onClicked: root.refreshCaptureTargets()
            }
            ThemedButton {
                visible: root.portalCapture
                variant: "primary"
                text: qsTr("Choose…")
                onClicked: ClipEditTools.pickCapture(root.nodeId)
            }
        }
    }

    Component {
        id: canvasPane
        GridLayout {
            columns: 2
            columnSpacing: Theme.spacingLg
            rowSpacing: Theme.spacingLg

            ThemedLabel { text: qsTr("Width") }
            ThemedNumberField {
                Layout.fillWidth: true
                from: 16
                to: 16384
                unit: "px"
                value: root.canvasWidth
                onEdited: v => root.canvasWidth = Math.round(v)
            }

            ThemedLabel { text: qsTr("Height") }
            ThemedNumberField {
                Layout.fillWidth: true
                from: 16
                to: 16384
                unit: "px"
                value: root.canvasHeight
                onEdited: v => root.canvasHeight = Math.round(v)
            }

            ThemedLabel { text: qsTr("Fill") }
            ThemedComboBox {
                Layout.fillWidth: true
                model: [qsTr("Checkered"), qsTr("Transparent"), qsTr("Color")]
                currentIndex: root.canvasFill
                onActivated: index => root.canvasFill = index
            }

            ThemedLabel { text: qsTr("Color") }
            ThemedButton {
                enabled: root.canvasFill === 2
                text: String(root.canvasColor).toUpperCase()
                onClicked: {
                    canvasColorDialog.selectedColor = root.canvasColor
                    canvasColorDialog.open()
                }
            }
        }
    }

    Component {
        id: ndiPane
        ColumnLayout {
            spacing: Theme.spacingLg

            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                text: !ClipEditTools.ndiAvailable() ? qsTr("NDI is not available in this build.")
                    : root.ndiSources.length === 0 ? qsTr("No NDI sources found on the network.")
                    : qsTr("Choose NDI source:")
            }
            ThemedComboBox {
                Layout.fillWidth: true
                visible: root.ndiSources.length > 0
                model: root.ndiSources
                currentIndex: root.ndiIndex
                onActivated: index => root.ndiIndex = index
            }
            ThemedButton {
                visible: ClipEditTools.ndiAvailable()
                text: qsTr("Refresh")
                onClicked: root.refreshNdi()
            }
        }
    }
}

pragma ComponentBehavior: Bound
import QtQuick
import Prism

// One node of the graph. Instantiated by the canvas for every row of controller.nodes; the card
// chrome (header, outline, ports, drag) is shared and the type-specific content is a body from
// the 13 *Body.qml files. Port rows live in node-local coordinates supplied by the model.
Item {
    id: root

    required property var model
    required property var canvas

    readonly property var controller: canvas.controller
    readonly property var nodeId: model.nodeId
    readonly property int type: model.type
    readonly property var meta: NodePalette.meta[type]

    // Model data is re-delivered as fresh objects on every row change (a drag re-emits the whole
    // row), which would rebuild every Repeater inside the bodies. These copies only change when
    // the content does.
    property var params: ({})
    property var inputPorts: []
    property var outputPorts: []
    property string _paramsKey: ""
    property string _inKey: ""
    property string _outKey: ""

    readonly property var rawParams: model.params
    readonly property var rawInputPorts: model.inputPorts
    readonly property var rawOutputPorts: model.outputPorts
    onRawParamsChanged: _syncParams()
    onRawInputPortsChanged: _syncInputs()
    onRawOutputPortsChanged: _syncOutputs()

    function _syncParams() {
        const key = JSON.stringify(rawParams)
        if (key === _paramsKey)
            return
        _paramsKey = key
        params = rawParams
    }
    function _syncInputs() {
        const key = JSON.stringify(rawInputPorts)
        if (key === _inKey)
            return
        _inKey = key
        inputPorts = rawInputPorts
    }
    function _syncOutputs() {
        const key = JSON.stringify(rawOutputPorts)
        if (key === _outKey)
            return
        _outKey = key
        outputPorts = rawOutputPorts
    }

    readonly property bool selected: canvas.selectedMap[nodeId] === true
    readonly property bool hasEditor: (type === NodePalette.nodeType.Process || type === NodePalette.nodeType.AudioEffect)
                                      ? NodeCatalog.effectHasEditor(type, params.effect)
                                      : true
    readonly property real headerH: meta.header
    readonly property string headerText: {
        switch (type) {
        case NodePalette.nodeType.Input:
            return params.audioOnly ? qsTr("Audio") : qsTr("Input")
        case NodePalette.nodeType.Layer:
            return model.title + "  " + params.canvasW + "×" + params.canvasH
        }
        return model.title
    }

    function requestEditor(kind) {
        if (kind.length > 0)
            canvas.openEditorRequested(nodeId, kind)
    }

    function activate() {
        requestEditor(NodePalette.editorKind(type, hasEditor))
    }

    x: model.x
    y: model.y
    width: model.width
    height: model.height
    z: selected ? 2 : 1

    onXChanged: canvas.scheduleBounds()
    onYChanged: canvas.scheduleBounds()
    onWidthChanged: canvas.scheduleBounds()
    onHeightChanged: canvas.scheduleBounds()
    Component.onCompleted: {
        _syncParams()
        _syncInputs()
        _syncOutputs()
        canvas.scheduleBounds()
        canvas.noteNodesChanged()
    }
    Component.onDestruction: {
        if (canvas) {
            canvas.scheduleBounds()
            canvas.noteNodesChanged()
        }
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radiusLg
        color: Theme.panelAccent
        border.width: 1
        border.color: Theme.panelBorder
    }

    Item {
        id: header
        width: parent.width
        height: root.headerH

        Rectangle {
            anchors.fill: parent
            radius: card.radius
            color: Theme.panelSecondaryBg
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: card.radius
            color: Theme.panelSecondaryBg
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.panelBorder
        }

        IconGlyph {
            id: headerIcon
            x: root.type === NodePalette.nodeType.Output ? 44 : Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            glyph: root.meta.icon
            iconSize: Theme.iconSizeMd
            iconColor: NodePalette.desat(Qt.color(root.meta.hue), 0.65)
        }

        NodeLabel {
            anchors.left: headerIcon.right
            anchors.leftMargin: Theme.spacingMd
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            text: root.headerText
            font.weight: Font.DemiBold
        }
    }

    // Drag / select / context menu. Declared before the body so interactive controls win.
    MouseArea {
        id: dragArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true

        property var startWorld: Qt.point(0, 0)
        property var dragIds: []
        property var dragStarts: []
        property bool moved: false
        property bool additive: false

        onPressed: (mouse) => {
            root.canvas.forceActiveFocus()
            root.canvas.selectedEdge = 0
            additive = (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
            if (mouse.button === Qt.RightButton) {
                if (!root.selected)
                    root.controller.select([root.nodeId])
                root.canvas.nodeMenuRequested(root, Qt.point(mouse.x + root.x, mouse.y + root.y),
                                              mapToItem(root.canvas, mouse.x, mouse.y))
                return
            }
            if (additive) {
                const cur = root.controller.selection.slice()
                const at = cur.indexOf(root.nodeId)
                if (at >= 0)
                    cur.splice(at, 1)
                else
                    cur.push(root.nodeId)
                root.controller.select(cur, false)
            } else if (!root.selected) {
                root.controller.select([root.nodeId])
            }
            moved = false
            startWorld = mapToItem(root.canvas.world, mouse.x, mouse.y)
            dragIds = root.selected ? root.controller.selection.slice() : []
            if (dragIds.length === 0)
                return
            dragStarts = dragIds.map((id) => {
                const it = root.canvas.nodeItem(id)
                return it ? [it.x, it.y] : [0, 0]
            })
        }

        onPositionChanged: (mouse) => {
            if (!pressed || dragIds.length === 0 || !(pressedButtons & Qt.LeftButton))
                return
            const w = mapToItem(root.canvas.world, mouse.x, mouse.y)
            const dx = w.x - startWorld.x
            const dy = w.y - startWorld.y
            if (!moved) {
                if (Math.hypot(dx, dy) * root.canvas.zoom < 4)
                    return
                moved = true
                root.canvas.userMoved = true
            }
            root.canvas.dragNodes(dragIds, dragStarts, dx, dy, root)
        }

        onReleased: (mouse) => {
            if (mouse.button !== Qt.LeftButton)
                return
            if (moved) {
                root.canvas.finishNodeDrag(root)
            } else if (!additive && root.controller.selection.length > 1) {
                root.controller.select([root.nodeId])
            }
            moved = false
        }

        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton)
                root.activate()
        }

        ThemedToolTip {
            text: NodePalette.tooltip(root.type)
            visible: text.length > 0 && dragArea.containsMouse && !dragArea.pressed
        }
    }

    Component { id: inputBody; InputBody { node: root } }
    Component { id: processBody; ProcessBody { node: root } }
    Component { id: audioEffectBody; AudioEffectBody { node: root } }
    Component { id: layerBody; LayerBody { node: root } }
    Component { id: abSelectBody; AbSelectBody { node: root } }
    Component { id: outputBody; OutputBody { node: root } }
    Component { id: audioMixerBody; AudioMixerBody { node: root } }
    Component { id: masterAudioOutputBody; MasterAudioOutputBody { node: root } }
    Component { id: masterAudioInputBody; MasterAudioInputBody { node: root } }
    Component { id: masterAudioCaptureBody; MasterAudioCaptureBody { node: root } }
    Component { id: scriptBody; ScriptBody { node: root } }
    Component { id: audioScriptBody; AudioScriptBody { node: root } }
    Component { id: triggerBody; TriggerBody { node: root } }

    Loader {
        anchors.fill: parent
        sourceComponent: {
            switch (root.type) {
            case NodePalette.nodeType.Input: return inputBody
            case NodePalette.nodeType.Process: return processBody
            case NodePalette.nodeType.AudioEffect: return audioEffectBody
            case NodePalette.nodeType.Layer: return layerBody
            case NodePalette.nodeType.AbSelect: return abSelectBody
            case NodePalette.nodeType.Output: return outputBody
            case NodePalette.nodeType.AudioMixer: return audioMixerBody
            case NodePalette.nodeType.MasterAudioOutput: return masterAudioOutputBody
            case NodePalette.nodeType.MasterAudioInput: return masterAudioInputBody
            case NodePalette.nodeType.MasterAudioCapture: return masterAudioCaptureBody
            case NodePalette.nodeType.Script: return scriptBody
            case NodePalette.nodeType.AudioScript: return audioScriptBody
            case NodePalette.nodeType.Trigger: return triggerBody
            }
            return null
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: card.radius
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        visible: root.selected
    }

    Repeater {
        model: root.inputPorts
        delegate: PortItem {
            required property var modelData
            canvas: root.canvas
            node: root
            isOutput: false
            portIndex: modelData.index
            kind: modelData.kind
            connected: modelData.connected
            label: modelData.label
            x: modelData.x - radius
            y: modelData.y - radius
        }
    }

    Repeater {
        model: root.outputPorts
        delegate: PortItem {
            required property var modelData
            canvas: root.canvas
            node: root
            isOutput: true
            portIndex: modelData.index
            kind: modelData.kind
            connected: modelData.connected
            label: modelData.label
            x: modelData.x - radius
            y: modelData.y - radius
        }
    }
}

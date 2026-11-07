pragma ComponentBehavior: Bound
import QtQuick
import Prism

// A node group. Collapsed, it is one box standing in for its members, with a proxy port for every
// edge that crosses the group boundary; connecting to a proxy connects the member port behind it.
// Expanded, it is a tinted backdrop around the members with a header to rename, drag and collapse.
// The group id shares the node id space, so ports and wires address this item like a node.
Item {
    id: root

    required property var model
    required property var canvas

    readonly property var controller: canvas.controller
    readonly property var nodeId: model.groupId
    readonly property bool collapsed: model.collapsed
    readonly property bool selected: controller.selectedGroup == nodeId
    readonly property color tint: model.color.length > 0 ? Qt.color(model.color) : Theme.mutedForeground
    readonly property real headerH: 26
    property bool editingName: false

    property var inputPorts: []
    property var outputPorts: []
    property string _inKey: ""
    property string _outKey: ""
    readonly property var rawInputPorts: model.inputPorts
    readonly property var rawOutputPorts: model.outputPorts
    onRawInputPortsChanged: _syncInputs()
    onRawOutputPortsChanged: _syncOutputs()

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

    function beginNameEdit() {
        nameEdit.text = model.name
        editingName = true
        nameEdit.forceActiveFocus()
        nameEdit.selectAll()
    }

    function commitName() {
        if (!editingName)
            return
        editingName = false
        if (nameEdit.text !== model.name)
            controller.setGroupProps(nodeId, { name: nameEdit.text })
        canvas.forceActiveFocus()
    }

    x: model.x
    y: model.y
    width: model.width
    height: model.height
    z: collapsed ? (selected ? 2 : 1) : 0

    onXChanged: canvas.scheduleBounds()
    onYChanged: canvas.scheduleBounds()
    onWidthChanged: canvas.scheduleBounds()
    onHeightChanged: canvas.scheduleBounds()
    onCollapsedChanged: canvas.scheduleBounds()
    Component.onCompleted: {
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
        anchors.fill: parent
        radius: Theme.radiusLg
        color: root.collapsed ? Theme.panelAccent : Qt.alpha(root.tint, 0.08)
        border.width: root.selected && !root.collapsed ? 2 : 1
        border.color: root.selected && !root.collapsed ? Theme.primary
                                                       : (root.collapsed ? Theme.panelBorder : Qt.alpha(root.tint, 0.45))
    }

    Rectangle {
        id: header
        width: parent.width
        height: root.headerH
        radius: Theme.radiusLg
        color: root.collapsed ? Qt.alpha(root.tint, 0.4) : Qt.alpha(root.tint, 0.2)

        IconGlyph {
            id: headerIcon
            x: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            glyph: "group"
            iconSize: Theme.iconSizeMd
            iconColor: Theme.panelForeground
        }

        NodeLabel {
            visible: !root.editingName
            anchors.left: headerIcon.right
            anchors.leftMargin: Theme.spacingMd
            anchors.right: countLabel.left
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            text: root.model.name
            font.weight: Font.DemiBold
        }

        TextInput {
            id: nameEdit
            visible: root.editingName
            anchors.left: headerIcon.right
            anchors.leftMargin: Theme.spacingMd
            anchors.right: countLabel.left
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.DemiBold
            color: Theme.panelForeground
            selectByMouse: true
            clip: true
            onAccepted: root.commitName()
            onActiveFocusChanged: if (!activeFocus) root.commitName()
            Keys.onEscapePressed: {
                root.editingName = false
                root.canvas.forceActiveFocus()
            }
        }

        NodeLabel {
            id: countLabel
            anchors.right: toggle.left
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            muted: true
            text: qsTr("%n node(s)", "", root.model.memberCount)
        }

        IconGlyph {
            id: toggle
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            glyph: root.collapsed ? "maximize-2" : "minimize-2"
            iconSize: Theme.iconSizeMd
            iconColor: toggleArea.containsMouse ? Theme.primary : Theme.mutedForeground
        }
    }

    // Drag / select / context menu. The collapsed box drags by its whole body, the backdrop by its
    // header only so the members inside stay clickable.
    MouseArea {
        id: dragArea
        width: parent.width
        height: root.collapsed ? parent.height : root.headerH
        enabled: !root.editingName
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        property point startWorld: Qt.point(0, 0)
        property point startPos: Qt.point(0, 0)
        property bool moved: false

        onPressed: (mouse) => {
            root.canvas.forceActiveFocus()
            root.canvas.selectedEdge = 0
            root.controller.selectGroup(root.nodeId)
            if (mouse.button === Qt.RightButton) {
                root.canvas.groupMenuRequested(root.nodeId, mapToItem(root.canvas, mouse.x, mouse.y))
                return
            }
            moved = false
            startWorld = mapToItem(root.canvas.world, mouse.x, mouse.y)
            startPos = Qt.point(root.x, root.y)
        }
        onPositionChanged: (mouse) => {
            if (!pressed || !(pressedButtons & Qt.LeftButton))
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
            root.controller.moveGroup(root.nodeId, startPos.x + dx, startPos.y + dy)
        }
        onReleased: root.controller.endMove()
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton)
                root.controller.toggleGroupCollapsed(root.nodeId)
        }
    }

    MouseArea {
        id: toggleArea
        x: toggle.x - 4
        y: header.y + toggle.y - 4
        width: toggle.width + 8
        height: toggle.height + 8
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.canvas.forceActiveFocus()
            root.controller.toggleGroupCollapsed(root.nodeId)
        }

        ThemedToolTip {
            text: root.collapsed ? qsTr("Expand (Tab)") : qsTr("Collapse (Tab)")
            visible: toggleArea.containsMouse
        }
    }

    Repeater {
        model: root.collapsed ? root.inputPorts : []
        delegate: NodeLabel {
            required property var modelData
            x: Theme.spacingLg
            y: modelData.y - height / 2
            width: root.width / 2 - Theme.spacingLg - Theme.spacingSm
            height: 14
            text: modelData.label
        }
    }

    Repeater {
        model: root.collapsed ? root.outputPorts : []
        delegate: NodeLabel {
            required property var modelData
            x: root.width / 2 + Theme.spacingSm
            y: modelData.y - height / 2
            width: root.width / 2 - Theme.spacingLg - Theme.spacingSm
            height: 14
            horizontalAlignment: Text.AlignRight
            text: modelData.label
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLg
        color: "transparent"
        border.width: 2
        border.color: Theme.primary
        visible: root.collapsed && root.selected
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

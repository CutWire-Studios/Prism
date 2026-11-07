pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Prism

// The node graph editor panel (workspace panel type "nodeGraph"). Wraps NodeCanvas with the
// toolbar, zoom bar, minimap, add-node menu and context menus. Node properties live in the
// inspector panel; the panel asks the shell to bring it forward via inspectorRequested.
Item {
    id: root

    // ClipGraphController
    property var controller: null

    property alias zoom: graphCanvas.zoom
    property bool showMinimap: true

    signal inspectorRequested()
    signal outputWindowRequested()
    signal runScriptRequested(var nodeId)
    // Local media files dropped on the canvas; x/y are graph coordinates.
    signal filesDropped(var urls, real x, real y)

    function frameAll() { graphCanvas.fitAll() }

    NodeCanvas {
        id: graphCanvas
        anchors.fill: parent
        controller: root.controller

        onInspectorRequested: root.inspectorRequested()
        onOutputWindowRequested: root.outputWindowRequested()

        onAddMenuRequested: (screenPos, worldPos) => {
            addMenu.pendingKind = -1
            addMenu.edgeId = graphCanvas.selectedEdge || graphCanvas.hoverEdge
            addMenu.worldPos = worldPos
            addMenu.openAt(graphCanvas, screenPos.x, screenPos.y)
        }
        onConnectionDropped: (screenPos, worldPos, kind, fromOutput) => {
            quickAdd.pendingKind = kind
            quickAdd.pendingIsOutput = fromOutput
            quickAdd.worldPos = worldPos
            quickAdd.openAt(screenPos.x, screenPos.y)
        }
        onQuickAddRequested: (screenPos, worldPos) => {
            quickAdd.pendingKind = -1
            quickAdd.worldPos = worldPos
            quickAdd.openAt(screenPos.x, screenPos.y)
        }
        onNodeMenuRequested: (item, worldPos, screenPos) => {
            nodeMenu.target = item
            nodeMenu.popup(graphCanvas, screenPos.x, screenPos.y)
        }
        onEdgeMenuRequested: (edgeId, screenPos) => {
            edgeMenu.edgeId = edgeId
            edgeMenu.popup(graphCanvas, screenPos.x, screenPos.y)
        }
        onFrameMenuRequested: (frameId, screenPos) => frameMenu.show(frameId, screenPos)
        onGroupMenuRequested: (groupId, screenPos) => groupMenu.show(groupId, screenPos)
    }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (drop) => {
            if (!drop.hasUrls)
                return
            const w = graphCanvas.toWorld(drop.x, drop.y)
            root.filesDropped(drop.urls, w.x, w.y)
            drop.accept()
        }
    }

    NodeLabel {
        anchors.centerIn: parent
        visible: graphCanvas.nodeCount <= 1
        width: Math.min(parent.width - 48, 360)
        muted: true
        wrapMode: Text.WordWrap
        maximumLineCount: 3
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("Drop media here, drag from the Media bin, or right-click to add a node")
    }

    // Undo / add / frame.
    Rectangle {
        x: Theme.spacingLg
        y: Theme.spacingLg
        width: toolRow.width + 2 * Theme.spacingSm
        height: toolRow.height + 2 * Theme.spacingSm
        radius: Theme.radiusMd
        color: Qt.alpha(Theme.panelBackground, 0.9)
        border.width: 1
        border.color: Theme.panelBorder

        Row {
            id: toolRow
            anchors.centerIn: parent
            spacing: Theme.spacingXs

            IconButton {
                glyph: "undo"
                tooltip: qsTr("Undo (Ctrl+Z)")
                focusPolicy: Qt.NoFocus
                enabled: !!root.controller && root.controller.canUndo
                onClicked: root.controller.undo()
            }
            IconButton {
                glyph: "redo"
                tooltip: qsTr("Redo (Ctrl+Shift+Z)")
                focusPolicy: Qt.NoFocus
                enabled: !!root.controller && root.controller.canRedo
                onClicked: root.controller.redo()
            }
            IconButton {
                glyph: "plus"
                tooltip: qsTr("Add node")
                focusPolicy: Qt.NoFocus
                enabled: !!root.controller
                onClicked: {
                    addMenu.pendingKind = -1
                    addMenu.edgeId = graphCanvas.selectedEdge
                    const c = graphCanvas.toWorld(graphCanvas.width / 2, graphCanvas.height / 2)
                    addMenu.worldPos = Qt.point(c.x - 60, c.y - 40)
                    addMenu.openAt(graphCanvas, Theme.spacingLg, 52)
                }
            }
            IconButton {
                glyph: "maximize"
                tooltip: qsTr("Frame all (F frames the selection)")
                focusPolicy: Qt.NoFocus
                onClicked: graphCanvas.fitAll()
            }
        }
    }

    // Zoom.
    Rectangle {
        x: Theme.spacingLg
        y: parent.height - height - Theme.spacingLg
        width: zoomRow.width + 2 * Theme.spacingSm
        height: zoomRow.height + 2 * Theme.spacingSm
        radius: Theme.radiusMd
        color: Qt.alpha(Theme.panelBackground, 0.9)
        border.width: 1
        border.color: Theme.panelBorder

        Row {
            id: zoomRow
            anchors.centerIn: parent
            spacing: Theme.spacingXs

            IconButton {
                glyph: "zoom-out"
                tooltip: qsTr("Zoom out (Ctrl+-)")
                focusPolicy: Qt.NoFocus
                enabled: graphCanvas.zoom > graphCanvas.minZoom + 0.01
                onClicked: graphCanvas.zoomCentered(1 / 1.12)
            }
            NodeLabel {
                width: 44
                height: Theme.iconButtonSize
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.monoFontFamily
                text: Math.round(graphCanvas.zoom * 100) + "%"
            }
            IconButton {
                glyph: "rotate-ccw"
                tooltip: qsTr("Reset zoom to 100% (Ctrl+0)")
                focusPolicy: Qt.NoFocus
                onClicked: graphCanvas.resetZoom()
            }
            IconButton {
                glyph: "zoom-in"
                tooltip: qsTr("Zoom in (Ctrl++)")
                focusPolicy: Qt.NoFocus
                enabled: graphCanvas.zoom < graphCanvas.maxZoom - 0.01
                onClicked: graphCanvas.zoomCentered(1.12)
            }
        }
    }

    NodeMinimap {
        visible: root.showMinimap
        x: parent.width - width - Theme.spacingLg
        y: parent.height - height - Theme.spacingLg
        canvas: graphCanvas
    }

    AddNodeMenu {
        id: addMenu
        canvas: graphCanvas

        onClosed: {
            if (!wasPicked)
                graphCanvas.cancelConnection()
            graphCanvas.forceActiveFocus()
        }
    }

    QuickAddPopup {
        id: quickAdd
        canvas: graphCanvas

        onClosed: {
            if (!wasPicked)
                graphCanvas.cancelConnection()
            graphCanvas.forceActiveFocus()
        }
    }

    ThemedContextMenu {
        id: nodeMenu

        property var target: null
        readonly property int type: target ? target.type : -1
        readonly property var nodeTypes: NodePalette.nodeType

        ThemedMenuItem {
            visible: nodeMenu.type === nodeMenu.nodeTypes.Script
            text: qsTr("Run now")
            onTriggered: root.runScriptRequested(nodeMenu.target.nodeId)
        }
        ThemedMenuItem {
            id: bypassItem
            visible: nodeMenu.type === nodeMenu.nodeTypes.Process || nodeMenu.type === nodeMenu.nodeTypes.AudioEffect
            text: qsTr("Bypass")
            checkable: true
            onTriggered: root.controller.toggleBypass(root.controller.selection)

            // A click overwrites a plain `checked:` binding; this keeps following the node.
            Binding {
                target: bypassItem
                property: "checked"
                value: nodeMenu.target ? !!nodeMenu.target.params.bypass : false
            }
        }
        ThemedMenuItem {
            visible: nodeMenu.type === nodeMenu.nodeTypes.Input || nodeMenu.type === nodeMenu.nodeTypes.Process
                || nodeMenu.type === nodeMenu.nodeTypes.Layer
            text: qsTr("Preview here")
            onTriggered: AppController.previewNode(nodeMenu.target.nodeId)
        }
        ThemedMenuItem {
            text: qsTr("Add frame around selection")
            onTriggered: root.controller.frameSelection()
        }
        ThemedMenuItem {
            visible: !!root.controller && root.controller.selection.length > 1
            text: qsTr("Group selection (Ctrl+G)")
            onTriggered: root.controller.groupSelection()
        }
        ThemedMenuItem {
            text: qsTr("Delete")
            icon.name: "trash-2"
            enabled: nodeMenu.type !== nodeMenu.nodeTypes.Output || graphCanvas.nodeCount <= 1
            onTriggered: root.controller.removeNodes(root.controller.selection)
        }
        onClosed: graphCanvas.forceActiveFocus()
    }

    ThemedContextMenu {
        id: edgeMenu

        property var edgeId: 0

        ThemedMenuItem {
            text: qsTr("Delete connection")
            icon.name: "trash-2"
            onTriggered: {
                root.controller.disconnectEdge(edgeMenu.edgeId)
                graphCanvas.selectedEdge = 0
            }
        }
        onClosed: graphCanvas.forceActiveFocus()
    }

    CascadeMenu {
        id: groupMenu

        function show(groupId, pos) {
            const item = graphCanvas.groupItem(groupId)
            model = [
                { text: qsTr("Rename"), action: () => { if (item) item.beginNameEdit() } },
                { text: item && item.collapsed ? qsTr("Expand (Tab)") : qsTr("Collapse (Tab)"),
                  action: () => root.controller.toggleGroupCollapsed(groupId) },
                { text: qsTr("Colour"), children: NodePalette.frameColors.map(c => ({
                      text: c.length > 0 ? c : qsTr("None"),
                      action: () => root.controller.setGroupProps(groupId, { color: c }) })) },
                { text: qsTr("Ungroup (Ctrl+Shift+G)"), action: () => root.controller.ungroup(groupId) },
                { separator: true },
                { text: qsTr("Delete group and its nodes"), icon: "trash-2",
                  action: () => root.controller.removeNodes(root.controller.groupMembers(groupId).map(m => m.id)) }
            ]
            popupAt(graphCanvas, pos.x, pos.y)
        }

        onClosed: graphCanvas.forceActiveFocus()
    }

    CascadeMenu {
        id: frameMenu

        function show(frameId, pos) {
            model = [
                { text: qsTr("Rename"), action: () => {
                      const item = graphCanvas.frameItem(frameId)
                      if (item)
                          item.beginTitleEdit()
                  } },
                { text: qsTr("Edit comment"), action: () => {
                      const item = graphCanvas.frameItem(frameId)
                      if (item)
                          item.beginCommentEdit()
                  } },
                { text: qsTr("Colour"), children: NodePalette.frameColors.map(c => ({
                      text: c.length > 0 ? c : qsTr("None"),
                      action: () => root.controller.setFrameProps(frameId, { color: c }) })) },
                { separator: true },
                { text: qsTr("Delete frame"), icon: "trash-2", action: () => root.controller.removeFrame(frameId) }
            ]
            popupAt(graphCanvas, pos.x, pos.y)
        }

        onClosed: graphCanvas.forceActiveFocus()
    }
}

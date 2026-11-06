pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Prism

// Searchable list of node types grouped by category. Opened from the canvas context menu, or
// from a connection dropped on empty space: then pendingKind is the dragged port's PortKind and
// only types with a compatible port on the opposite side are listed.
Popup {
    id: root

    required property var canvas
    // -1 for a plain add; otherwise the PortKind of the dragged port.
    property int pendingKind: -1
    property bool pendingIsOutput: false
    // Where the node is created, in world coordinates.
    property point worldPos: Qt.point(0, 0)
    property bool wasPicked: false

    signal picked(var entry)

    function openAt(sx, sy) {
        wasPicked = false
        search.text = ""
        rebuild()
        x = Math.max(8, Math.min(sx, (parent ? parent.width : sx) - width - 8))
        y = Math.max(8, Math.min(sy, (parent ? parent.height : sy) - height - 8))
        open()
    }

    property var rows: []
    property int highlighted: -1

    function allEntries() {
        const T = NodePalette.nodeType
        const list = []
        list.push({ label: qsTr("Input Node…"), category: qsTr("Source"), type: T.Input, input: true })
        for (const fx of NodeCatalog.processEffects())
            list.push({ label: fx.menuLabel, category: qsTr("Process"), type: T.Process, params: { effect: fx.id } })
        list.push({ label: qsTr("Layer"), category: qsTr("Switching"), type: T.Layer })
        list.push({ label: qsTr("A/B Deck Select"), category: qsTr("Switching"), type: T.AbSelect })
        for (const fx of NodeCatalog.audioEffects())
            list.push({ label: fx.menuLabel, category: qsTr("Audio Effects"), type: T.AudioEffect, params: { effect: fx.id } })
        list.push({ label: qsTr("Audio Output"), category: qsTr("Audio"), type: T.MasterAudioOutput, unique: true })
        list.push({ label: qsTr("Audio Capture"), category: qsTr("Audio"), type: T.MasterAudioCapture })
        list.push({ label: qsTr("Mic Input"), category: qsTr("Audio"), type: T.MasterAudioInput })
        list.push({ label: qsTr("Audio Mixer"), category: qsTr("Audio"), type: T.AudioMixer })
        if (NodeCatalog.scriptingAvailable()) {
            list.push({ label: qsTr("Script Node"), category: qsTr("Script"), type: T.Script })
            list.push({ label: qsTr("Audio Script Node"), category: qsTr("Script"), type: T.AudioScript })
            list.push({ label: qsTr("Trigger Node"), category: qsTr("Script"), type: T.Trigger })
        }
        return list
    }

    function compatible(entry) {
        if (pendingKind < 0)
            return true
        if (entry.input)
            return false
        const meta = NodePalette.meta[entry.type]
        const kinds = pendingIsOutput ? meta.inKinds : meta.outKinds
        for (const k of kinds) {
            if (NodeCatalog.portsCompatible(pendingKind, k))
                return true
        }
        return false
    }

    function rebuild() {
        const q = search.text.trim().toLowerCase()
        const out = []
        let lastCategory = ""
        for (const e of allEntries()) {
            if (e.unique && canvas.hasNodeOfType(e.type))
                continue
            if (!compatible(e))
                continue
            if (q.length > 0 && e.label.toLowerCase().indexOf(q) < 0
                    && e.category.toLowerCase().indexOf(q) < 0)
                continue
            if (e.category !== lastCategory) {
                out.push({ header: true, label: e.category })
                lastCategory = e.category
            }
            out.push(e)
        }
        rows = out
        highlighted = -1
        for (let i = 0; i < out.length; ++i) {
            if (!out[i].header) {
                highlighted = i
                break
            }
        }
    }

    function step(delta) {
        let i = highlighted
        for (;;) {
            i += delta
            if (i < 0 || i >= rows.length)
                return
            if (!rows[i].header)
                break
        }
        highlighted = i
        list.positionViewAtIndex(i, ListView.Contain)
    }

    function pick(entry) {
        wasPicked = true
        close()
        picked(entry)
    }

    width: 240
    height: Math.min(360, spacer.height + list.contentHeight + padding * 2 + Theme.spacingSm)
    padding: Theme.spacingSm
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onOpened: search.forceActiveFocus()

    background: Rectangle {
        radius: Theme.c.themedContextMenu.radius
        color: Theme.c.themedContextMenu.background
        border.width: Theme.c.themedContextMenu.borderWidth
        border.color: Theme.c.themedContextMenu.border
    }

    contentItem: Item {
        ThemedTextField {
            id: search
            width: parent.width
            placeholderText: root.pendingKind >= 0 ? qsTr("Add and connect…") : qsTr("Search nodes…")
            onTextChanged: root.rebuild()
            Keys.onDownPressed: root.step(1)
            Keys.onUpPressed: root.step(-1)
            Keys.onReturnPressed: {
                if (root.highlighted >= 0)
                    root.pick(root.rows[root.highlighted])
            }
            Keys.onEnterPressed: {
                if (root.highlighted >= 0)
                    root.pick(root.rows[root.highlighted])
            }
        }

        Item {
            id: spacer
            width: parent.width
            height: search.height + Theme.spacingSm
        }

        NodeLabel {
            x: Theme.spacingLg
            y: spacer.height + Theme.spacingSm
            visible: root.rows.length === 0
            muted: true
            text: qsTr("No matching nodes")
        }

        ListView {
            id: list
            y: spacer.height
            width: parent.width
            height: parent.height - spacer.height
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: row

                required property var modelData
                required property int index

                width: ListView.view.width
                height: row.modelData.header ? Theme.controlHeightSm : Theme.controlHeightSm + Theme.spacingSm

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.c.themedMenuItem.radius
                    color: !row.modelData.header && (area.containsMouse || root.highlighted === row.index)
                           ? Theme.c.themedMenuItem.backgroundHighlighted : "transparent"
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.spacingLg
                    width: parent.width - 2 * Theme.spacingLg
                    text: row.modelData.label
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: row.modelData.header ? Font.DemiBold : Font.Normal
                    color: row.modelData.header ? Theme.c.themedMenuItem.sectionForeground
                                                : Theme.c.themedMenuItem.foreground
                }

                MouseArea {
                    id: area
                    anchors.fill: parent
                    enabled: !row.modelData.header
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: root.highlighted = row.index
                    onClicked: root.pick(row.modelData)
                }
            }
        }
    }
}

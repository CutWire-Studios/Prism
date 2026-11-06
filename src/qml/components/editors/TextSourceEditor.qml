pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// Text source editor: the template with {placeholder} highlighting and the connected script's
// variables as drag-and-click blocks, a Type / Look / Animate inspector and a live preview.
// openFor(nodeId) loads the Text node; OK (or Ctrl+Enter) writes it back to the graph.
ThemedDialog {
    id: root

    onClosed: AppController.cancelPendingSourcePos()

    title: qsTr("Text Source")
    preferredWidth: 1120
    showFooter: false
    showAccept: false
    showReject: false
    acceptOnReturn: false

    readonly property var st: backend.style
    readonly property bool canAccept: templateArea.text.trim().length > 0

    TextSourceBackend {
        id: backend
    }

    function openForNew() {
        openFor(0)
    }

    function openFor(nodeId) {
        backend.setHighlightColors(Theme.accentOnPanel, Theme.warning, Theme.c.themedLabel.foregroundMuted)
        backend.open(AppController.graph, nodeId)
        templateArea.text = backend.templateText()
        stack.currentIndex = 0
        root.open()
    }

    function commitAndClose() {
        if (!root.canAccept) {
            templateArea.forceActiveFocus()
            return
        }
        if (backend.commit())
            root.close()
    }

    function insertToken(token) {
        templateArea.insert(templateArea.cursorPosition, token)
        templateArea.forceActiveFocus()
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: root.visible
        onActivated: root.commitAndClose()
    }

    StyleNameDialog {
        id: saveDialog
        onNameChosen: function (name) { backend.saveStylePreset(name) }
    }

    StyleNameDialog {
        id: renameDialog
        property string targetId: ""
        onNameChosen: function (name) { backend.renamePack(renameDialog.targetId, name) }
    }

    ThemedContextMenu {
        id: packMenu
        property string targetId: ""

        ThemedMenuItem {
            text: qsTr("Rename…")
            onTriggered: {
                renameDialog.targetId = packMenu.targetId
                renameDialog.openWith(qsTr("Rename style"), backend.packLabel(packMenu.targetId))
            }
        }
        ThemedMenuItem {
            text: qsTr("Delete")
            onTriggered: backend.deletePack(packMenu.targetId)
        }
    }

    contentItem: ColumnLayout {
        width: parent ? parent.width : root.preferredWidth
        spacing: Theme.spacingLg

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(root.availableContentHeight - footerRow.implicitHeight - Theme.spacingLg, 640)
            spacing: Theme.spacingLg

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacingSm

                EditorImageItem {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 220
                    boxed: true
                    image: backend.preview
                }

                RowLayout {
                    Layout.fillWidth: true

                    ThemedButton {
                        variant: "secondary"
                        text: backend.playing ? qsTr("Stop") : qsTr("Play")
                        tooltip: qsTr("Preview In, hold, Out on a loop")
                        onClicked: backend.setPlaying(!backend.playing)
                    }
                    Item { Layout.fillWidth: true }
                }

                ThemedLabel {
                    text: qsTr("TEMPLATE")
                    font.weight: Font.DemiBold
                }

                Item {
                    id: templateHolder
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110

                    ThemedTextArea {
                        id: templateArea
                        anchors.fill: parent
                        color: "transparent"
                        selectedTextColor: "transparent"
                        placeholderText: "Hello {name}"
                        cursorDelegate: Rectangle {
                            width: 1
                            color: Theme.panelForeground
                            visible: templateArea.activeFocus
                        }
                        onTextChanged: backend.setTemplate(templateArea.text)
                    }

                    Text {
                        id: highlight
                        anchors.fill: parent
                        anchors.leftMargin: templateArea.leftPadding
                        anchors.rightMargin: templateArea.rightPadding
                        anchors.topMargin: templateArea.topPadding
                        anchors.bottomMargin: templateArea.bottomPadding
                        textFormat: Text.RichText
                        text: backend.templateHtml
                        font: templateArea.font
                        color: Theme.panelForeground
                        wrapMode: Text.Wrap
                    }

                    DropArea {
                        anchors.fill: parent
                        onDropped: function (drop) {
                            if (drop.hasText)
                                root.insertToken(drop.text)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        text: qsTr("SCRIPT VARIABLES")
                        font.weight: Font.DemiBold
                    }
                    Item { Layout.fillWidth: true }
                    ThemedButton {
                        visible: backend.canRunScript
                        variant: "ghost"
                        text: qsTr("Run script")
                        tooltip: qsTr("Execute the connected script now to refresh values")
                        onClicked: backend.runScript()
                    }
                }

                ThemedLabel {
                    Layout.fillWidth: true
                    visible: backend.scriptHint.length > 0
                    text: backend.scriptHint
                }

                Flow {
                    Layout.fillWidth: true
                    visible: backend.scriptVariables.length > 0
                    spacing: Theme.spacingSm

                    Repeater {
                        model: backend.scriptVariables

                        delegate: ThemedChip {
                            id: chip

                            required property var modelData

                            readonly property string token: "{" + modelData.name + "}"

                            text: token + "  " + modelData.value
                            tooltip: qsTr("Click or drag into the template to insert %1").arg(token)
                            onClicked: root.insertToken(chip.token)

                            DragHandler {
                                id: chipDrag
                                target: null
                            }
                            Drag.active: chipDrag.active
                            Drag.dragType: Drag.Automatic
                            Drag.supportedActions: Qt.CopyAction
                            Drag.mimeData: { "text/plain": chip.token }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 470
                Layout.fillHeight: true
                spacing: Theme.spacingSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    Repeater {
                        model: [qsTr("Type"), qsTr("Look"), qsTr("Animate")]

                        delegate: ThemedChip {
                            id: tabChip

                            required property var modelData
                            required property int index

                            Layout.fillWidth: true
                            text: tabChip.modelData
                            selected: stack.currentIndex === tabChip.index
                            onClicked: {
                                stack.currentIndex = tabChip.index
                                if (tabChip.index === 1)
                                    backend.refreshPacks()
                            }
                        }
                    }
                }

                StackLayout {
                    id: stack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: 0

                    Flickable {
                        contentHeight: typeColumn.implicitHeight
                        clip: true
                        ScrollBar.vertical: AppScrollBar { }

                        ColumnLayout {
                            id: typeColumn
                            width: parent.width
                            spacing: Theme.spacingMd

                            ThemedLabel { text: qsTr("FONT"); font.weight: Font.DemiBold }

                            StyleFieldRow {
                                Layout.fillWidth: true
                                label: qsTr("Font")

                                ThemedComboBox {
                                    id: fontCombo
                                    Layout.fillWidth: true
                                    model: backend.fontFamilies
                                    currentIndex: Math.max(0, fontCombo.find(root.st.fontFamily))
                                    onActivated: function (index) { backend.edit({ fontFamily: fontCombo.textAt(index) }) }
                                }
                            }

                            StyleFieldRow {
                                Layout.fillWidth: true
                                label: qsTr("Weight")

                                ThemedComboBox {
                                    Layout.fillWidth: true
                                    model: backend.options("weight")
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: indexOfValue(String(root.st.fontWeight))
                                    onActivated: backend.edit({ fontWeight: Number(currentValue) })
                                }
                                ThemedCheckBox {
                                    text: qsTr("Italic")
                                    checked: root.st.italic === true
                                    onClicked: backend.edit({ italic: checked })
                                }
                            }

                            StyleSliderRow {
                                Layout.fillWidth: true
                                label: qsTr("Size")
                                from: 8
                                to: 600
                                step: 1
                                decimals: 0
                                unit: "px"
                                value: root.st.pixelSize
                                onEdited: function (v) { backend.edit({ pixelSize: Math.round(v) }) }
                            }

                            StyleFieldRow {
                                Layout.fillWidth: true
                                label: qsTr("Align")

                                ThemedComboBox {
                                    Layout.fillWidth: true
                                    model: backend.options("align")
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: indexOfValue(root.st.align)
                                    onActivated: backend.edit({ align: currentValue })
                                }
                                ThemedComboBox {
                                    Layout.fillWidth: true
                                    model: backend.options("valign")
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: indexOfValue(root.st.valign)
                                    onActivated: backend.edit({ valign: currentValue })
                                }
                            }

                            ThemedLabel { text: qsTr("SPACING"); font.weight: Font.DemiBold }

                            ThemedCheckBox {
                                text: qsTr("Word wrap")
                                checked: root.st.wordWrap === true
                                onClicked: backend.edit({ wordWrap: checked })
                            }
                            StyleSliderRow {
                                Layout.fillWidth: true
                                label: qsTr("Line height")
                                from: 0.5
                                to: 3
                                step: 0.05
                                decimals: 2
                                value: root.st.lineHeight
                                onEdited: function (v) { backend.edit({ lineHeight: v }) }
                            }
                            StyleSliderRow {
                                Layout.fillWidth: true
                                label: qsTr("Letter spacing")
                                from: -20
                                to: 100
                                step: 0.5
                                decimals: 1
                                unit: "px"
                                value: root.st.letterSpacing
                                onEdited: function (v) { backend.edit({ letterSpacing: v }) }
                            }
                            StyleSliderRow {
                                Layout.fillWidth: true
                                label: qsTr("Bend")
                                from: -100
                                to: 100
                                step: 1
                                decimals: 0
                                value: root.st.pathBend
                                onEdited: function (v) { backend.edit({ pathBend: v }) }
                            }

                            ThemedLabel { text: qsTr("DECORATIONS"); font.weight: Font.DemiBold }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSm

                                ThemedCheckBox {
                                    text: qsTr("Background")
                                    checked: root.st.boxEnabled === true
                                    onClicked: backend.edit({ boxEnabled: checked })
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    enabled: root.st.boxEnabled === true
                                    spacing: Theme.spacingSm

                                    StyleFieldRow {
                                        Layout.fillWidth: true
                                        label: qsTr("Colour")
                                        StyleColorButton {
                                            swatch: root.st.boxColor
                                            onPicked: function (c) { backend.edit({ boxColor: String(c) }) }
                                        }
                                    }
                                    StyleSliderRow {
                                        Layout.fillWidth: true
                                        label: qsTr("Padding")
                                        from: 0
                                        to: 500
                                        step: 1
                                        decimals: 0
                                        unit: "px"
                                        value: root.st.boxPadding
                                        onEdited: function (v) { backend.edit({ boxPadding: v }) }
                                    }
                                    StyleSliderRow {
                                        Layout.fillWidth: true
                                        label: qsTr("Corner radius")
                                        from: 0
                                        to: 500
                                        step: 1
                                        decimals: 0
                                        unit: "px"
                                        value: root.st.boxRadius
                                        onEdited: function (v) { backend.edit({ boxRadius: v }) }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSm

                                ThemedCheckBox {
                                    text: qsTr("Word highlight")
                                    checked: root.st.wordHighlight.enabled === true
                                    onClicked: backend.edit({ wordHighlight: { enabled: checked } })
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    enabled: root.st.wordHighlight.enabled === true
                                    spacing: Theme.spacingSm

                                    StyleFieldRow {
                                        Layout.fillWidth: true
                                        label: qsTr("Colour")
                                        StyleColorButton {
                                            swatch: root.st.wordHighlight.color
                                            onPicked: function (c) { backend.edit({ wordHighlight: { color: String(c) } }) }
                                        }
                                    }
                                    StyleSliderRow {
                                        Layout.fillWidth: true
                                        label: qsTr("Thickness")
                                        from: 0
                                        to: 200
                                        step: 1
                                        decimals: 0
                                        unit: "px"
                                        value: root.st.wordHighlight.padding
                                        onEdited: function (v) { backend.edit({ wordHighlight: { padding: v } }) }
                                    }
                                    StyleSliderRow {
                                        Layout.fillWidth: true
                                        label: qsTr("Corner radius")
                                        from: 0
                                        to: 200
                                        step: 1
                                        decimals: 0
                                        unit: "px"
                                        value: root.st.wordHighlight.radius
                                        onEdited: function (v) { backend.edit({ wordHighlight: { radius: v } }) }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSm

                                ThemedCheckBox {
                                    text: qsTr("Underline")
                                    checked: root.st.underlineEnabled === true
                                    onClicked: backend.edit({ underlineEnabled: checked })
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    enabled: root.st.underlineEnabled === true
                                    spacing: Theme.spacingSm

                                    StyleFieldRow {
                                        Layout.fillWidth: true
                                        label: qsTr("Colour")
                                        StyleColorButton {
                                            swatch: root.st.underlineColor
                                            onPicked: function (c) { backend.edit({ underlineColor: String(c) }) }
                                        }
                                    }
                                    StyleSliderRow {
                                        Layout.fillWidth: true
                                        label: qsTr("Thickness")
                                        from: 0
                                        to: 100
                                        step: 1
                                        decimals: 0
                                        unit: "px"
                                        value: root.st.underlineWidth
                                        onEdited: function (v) { backend.edit({ underlineWidth: v }) }
                                    }
                                    StyleSliderRow {
                                        Layout.fillWidth: true
                                        label: qsTr("Offset")
                                        from: -200
                                        to: 200
                                        step: 1
                                        decimals: 0
                                        unit: "px"
                                        value: root.st.underlineOffset
                                        onEdited: function (v) { backend.edit({ underlineOffset: v }) }
                                    }
                                }
                            }

                            ThemedLabel { text: qsTr("WORD ACCENT"); font.weight: Font.DemiBold }

                            StyleFieldRow {
                                Layout.fillWidth: true
                                label: qsTr("Accent")

                                ThemedComboBox {
                                    Layout.fillWidth: true
                                    model: backend.options("accentRule")
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: indexOfValue(root.st.accent.rule)
                                    onActivated: backend.edit({ accent: { rule: currentValue } })
                                }
                            }

                            StyleSliderRow {
                                Layout.fillWidth: true
                                visible: root.st.accent.rule === "everyNth"
                                label: qsTr("Every N")
                                from: 1
                                to: 16
                                step: 1
                                decimals: 0
                                value: root.st.accent.n
                                onEdited: function (v) { backend.edit({ accent: { n: Math.round(v) } }) }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: root.st.accent.rule !== "none"
                                spacing: Theme.spacingMd

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingSm

                                    ThemedCheckBox {
                                        text: qsTr("Accent colour")
                                        checked: root.st.accent.colorEnabled === true
                                        onClicked: backend.edit({ accent: { colorEnabled: checked } })
                                    }
                                    StyleFieldRow {
                                        Layout.fillWidth: true
                                        enabled: root.st.accent.colorEnabled === true
                                        label: qsTr("Colour")
                                        StyleColorButton {
                                            swatch: root.st.accent.color
                                            onPicked: function (c) { backend.edit({ accent: { color: String(c) } }) }
                                        }
                                    }
                                }
                                StyleSliderRow {
                                    Layout.fillWidth: true
                                    label: qsTr("Accent size")
                                    from: 0.25
                                    to: 4
                                    step: 0.05
                                    decimals: 2
                                    unit: "x"
                                    value: root.st.accent.sizeScale
                                    onEdited: function (v) { backend.edit({ accent: { sizeScale: v } }) }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingSm

                                    ThemedCheckBox {
                                        text: qsTr("Accent outline")
                                        checked: root.st.accent.outlineEnabled === true
                                        onClicked: backend.edit({ accent: { outlineEnabled: checked } })
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        enabled: root.st.accent.outlineEnabled === true
                                        spacing: Theme.spacingSm

                                        StyleFieldRow {
                                            Layout.fillWidth: true
                                            label: qsTr("Colour")
                                            StyleColorButton {
                                                swatch: root.st.accent.outlineColor
                                                onPicked: function (c) { backend.edit({ accent: { outlineColor: String(c) } }) }
                                            }
                                        }
                                        StyleSliderRow {
                                            Layout.fillWidth: true
                                            label: qsTr("Width")
                                            from: 0
                                            to: 100
                                            step: 1
                                            decimals: 0
                                            unit: "px"
                                            value: root.st.accent.outlineWidth
                                            onEdited: function (v) { backend.edit({ accent: { outlineWidth: v } }) }
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingSm

                                    ThemedCheckBox {
                                        text: qsTr("Accent pill")
                                        checked: root.st.accent.highlight.enabled === true
                                        onClicked: backend.edit({ accent: { highlight: { enabled: checked } } })
                                    }
                                    StyleFieldRow {
                                        Layout.fillWidth: true
                                        enabled: root.st.accent.highlight.enabled === true
                                        label: qsTr("Colour")
                                        StyleColorButton {
                                            swatch: root.st.accent.highlight.color
                                            onPicked: function (c) { backend.edit({ accent: { highlight: { color: String(c) } } }) }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Flickable {
                        contentHeight: lookColumn.implicitHeight
                        clip: true
                        ScrollBar.vertical: AppScrollBar { }

                        ColumnLayout {
                            id: lookColumn
                            width: parent.width
                            spacing: Theme.spacingMd

                            ThemedLabel { text: qsTr("STYLE PACK"); font.weight: Font.DemiBold }

                            ThemedLabel {
                                visible: backend.userPacks.length > 0
                                text: qsTr("My styles")
                            }
                            StyleThumbGrid {
                                id: userGrid
                                Layout.fillWidth: true
                                visible: backend.userPacks.length > 0
                                items: backend.userPacks
                                currentId: root.st.packId || ""
                                onActivated: function (id) { backend.applyStylePack(id) }
                                onContextRequested: function (id, x, y) {
                                    packMenu.targetId = id
                                    packMenu.popup(userGrid, x, y)
                                }
                            }

                            ThemedLabel { text: qsTr("Built-in") }
                            StyleThumbGrid {
                                Layout.fillWidth: true
                                items: backend.builtinPacks
                                currentId: root.st.packId || ""
                                onActivated: function (id) { backend.applyStylePack(id) }
                            }

                            ThemedButton {
                                text: qsTr("Save style…")
                                variant: "secondary"
                                tooltip: qsTr("Keep the current style in My styles")
                                onClicked: saveDialog.openWith(qsTr("Save style"), "")
                            }

                            ThemedLabel { text: qsTr("LOOK"); font.weight: Font.DemiBold }

                            StyleThumbGrid {
                                Layout.fillWidth: true
                                items: backend.lookThumbs
                                currentId: root.st.lookId || ""
                                thumbSize: Qt.size(72, 40)
                                onActivated: function (id) { backend.applyLook(id) }
                            }

                            StyleParamForm {
                                Layout.fillWidth: true
                                visible: backend.lookSpecs.length > 0
                                specs: backend.lookSpecs
                                values: backend.lookValues
                                onChanged: function (id, v) { backend.setLookParam(id, v) }
                            }

                            ThemedLabel { text: qsTr("LAYERS"); font.weight: Font.DemiBold }

                            ShadingLayerStack {
                                Layout.fillWidth: true
                                layers: root.st.layers
                                scopeVisible: true
                                gradientSpaceVisible: true
                                onEdited: function (l) { backend.setLayers(l) }
                            }
                        }
                    }

                    Flickable {
                        contentHeight: animColumn.implicitHeight
                        clip: true
                        ScrollBar.vertical: AppScrollBar { }

                        ColumnLayout {
                            id: animColumn
                            width: parent.width
                            spacing: Theme.spacingMd

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingXs

                                Repeater {
                                    model: [qsTr("In"), qsTr("Out"), qsTr("Loop")]

                                    delegate: ThemedChip {
                                        id: slotChip

                                        required property var modelData
                                        required property int index

                                        Layout.fillWidth: true
                                        text: slotChip.modelData
                                        selected: backend.animSlot === slotChip.index
                                        onClicked: backend.animSlot = slotChip.index
                                    }
                                }
                            }

                            StyleFieldRow {
                                Layout.fillWidth: true
                                visible: backend.animCategoriesVisible
                                label: qsTr("Category")

                                ThemedComboBox {
                                    Layout.fillWidth: true
                                    model: backend.animCategories
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: indexOfValue(backend.animCategory)
                                    onActivated: backend.animCategory = currentValue
                                }
                            }

                            StyleThumbGrid {
                                Layout.fillWidth: true
                                items: backend.animPresets
                                currentId: backend.animPresetId
                                thumbSize: Qt.size(120, 68)
                                onActivated: function (id) {
                                    backend.applyAnimationPreset(id)
                                    if (id.length > 0)
                                        backend.setPlaying(true)
                                }
                            }

                            ThemedLabel {
                                Layout.fillWidth: true
                                visible: backend.animCustom
                                text: backend.animNote
                            }

                            ThemedButton {
                                visible: backend.animCustom
                                variant: "secondary"
                                text: qsTr("Revert to preset")
                                onClicked: backend.applyAnimationPreset("")
                            }

                            StyleParamForm {
                                Layout.fillWidth: true
                                visible: backend.animSpecs.length > 0
                                specs: backend.animSpecs
                                values: backend.animValues
                                disabledIds: backend.animDisabledParams
                                onChanged: function (id, v) { backend.setAnimParam(id, v) }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            id: footerRow
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacingLg

            ThemedButton {
                text: qsTr("Cancel")
                variant: "secondary"
                onClicked: root.close()
            }
            ThemedButton {
                text: qsTr("OK")
                variant: "primary"
                enabled: root.canAccept
                tooltip: qsTr("Ctrl+Enter")
                onClicked: root.commitAndClose()
            }
        }
    }
}

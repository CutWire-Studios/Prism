import QtQuick
import QtQuick.Layouts
import Prism

// Label, slider and editable number on one row: the widget editors' SliderSpinRow.
// Setting expressionName adds a small "f" toggle that swaps the slider for a Lua expression
// field (Blender-driver style); the owner stores the text from expressionEdited.
RowLayout {
    id: root

    property string label: ""
    property real from: 0
    property real to: 1
    property real step: 0.01
    property int decimals: 2
    property string unit: ""
    property real value: 0
    property string expressionName: ""
    property string expression: ""
    property string expressionError: ""
    property bool expressionOpen: false
    // Raw parameter units per displayed unit; a new modulator is scaled to from*exprScale..to*exprScale.
    property real exprScale: 1
    property var linkForm: null
    readonly property string linkedId: {
        const m = /^mod\((\d+)\)$/.exec(expression)
        return m ? m[1] : ""
    }
    readonly property bool exprMode: expressionName.length > 0 && (expressionOpen || expression.length > 0)

    signal edited(real value)
    signal expressionEdited(string expression)

    function roundTo(v) {
        const f = Math.pow(10, root.decimals)
        return Math.round(v * f) / f
    }

    spacing: Theme.spacingSm

    Component.onCompleted: {
        let p = root.parent
        while (p && p.bindModulator === undefined)
            p = p.parent
        linkForm = p
    }

    ThemedLabel {
        Layout.preferredWidth: 78
        Layout.maximumWidth: 78
        text: root.label
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
    }

    ThemedSlider {
        id: slider
        visible: !root.exprMode
        Layout.fillWidth: true
        label: root.label
        from: root.from
        to: root.to
        stepSize: root.step
        value: root.value
        onMoved: root.edited(root.roundTo(slider.value))
    }

    ThemedTextField {
        id: exprField
        visible: root.exprMode && root.linkedId.length === 0
        Layout.fillWidth: true
        placeholderText: qsTr("e.g. 0.5 + audio.low * 0.5")
        errorText: root.expressionError
        onEditingFinished: {
            if (text.trim() !== root.expression)
                root.expressionEdited(text.trim())
        }
    }

    Binding {
        target: exprField
        property: "text"
        when: !exprField.activeFocus
        value: root.expression
    }

    ThemedNumberField {
        id: field
        visible: !root.exprMode
        Layout.preferredWidth: 78
        from: root.from
        to: root.to
        step: root.step
        decimals: root.decimals
        value: root.value
        onEdited: function (v) { root.edited(root.roundTo(v)) }
    }

    ThemedLabel {
        visible: root.unit.length > 0
        text: root.unit
    }

    Rectangle {
        visible: root.linkedId.length > 0
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.controlHeightSm
        radius: height / 2
        color: Theme.panelSecondaryBg
        border.color: Theme.panelBorder

        ThemedLabel {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            anchors.right: unlinkButton.left
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
            text: root.linkForm && root.linkedId.length > 0 ? root.linkForm.modulatorLabel(root.linkedId) : ""
        }

        IconButton {
            id: unlinkButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            glyph: "unlink-2"
            tooltip: qsTr("Unlink from modulator")
            buttonSize: Theme.controlHeightSm
            onClicked: root.expressionEdited("")
        }
    }

    IconButton {
        visible: root.linkForm !== null && root.expressionName.length > 0 && root.linkedId.length === 0
        glyph: "link-2"
        tooltip: qsTr("Link to a modulator")
        buttonSize: Theme.controlHeightSm
        onClicked: linkMenu.popup()

        ModulatorLinkMenu {
            id: linkMenu
            onPicked: id => root.linkForm.bindModulator(root.expressionName, id,
                                                        root.from * root.exprScale, root.to * root.exprScale)
        }
    }

    ThemedToggleButton {
        visible: root.expressionName.length > 0
        text: "\u0192"
        tooltip: qsTr("Drive this value with a Lua expression")
        toggleHeight: Theme.controlHeightSm
        checkable: false
        checked: root.exprMode
        onClicked: {
            root.expressionOpen = !root.exprMode
            if (root.expressionOpen) {
                exprField.forceActiveFocus()
            } else if (root.expression.length > 0) {
                root.expressionEdited("")
            }
        }
    }
}

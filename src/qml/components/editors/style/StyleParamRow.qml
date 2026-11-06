import QtQuick
import QtQuick.Layouts
import Prism

// One parameter of a generated form, shaped by its spec (StyleLayers.specList). changed(id, value)
// carries a plain value; the backend converts it to the parameter's typed value.
ColumnLayout {
    id: root

    property var spec: ({})
    property var value
    readonly property string type: spec.type || ""
    readonly property var options: spec.options || []

    signal changed(string id, var value)

    Layout.fillWidth: true
    spacing: 0

    StyleSliderRow {
        Layout.fillWidth: true
        visible: root.type === "scalar"
        label: root.spec.label || ""
        from: root.spec.min !== undefined ? root.spec.min : 0
        to: root.spec.max !== undefined ? root.spec.max : 1
        step: root.spec.step !== undefined ? root.spec.step : 0.01
        decimals: root.spec.decimals !== undefined ? root.spec.decimals : 2
        unit: root.spec.unit || ""
        value: Number(root.value) || 0
        onEdited: function (v) { root.changed(root.spec.id, v) }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.type === "vec2"
        label: root.spec.label || ""

        ThemedNumberField {
            id: vx
            Layout.fillWidth: true
            from: root.spec.min !== undefined ? root.spec.min : -10000
            to: root.spec.max !== undefined ? root.spec.max : 10000
            step: root.spec.step !== undefined ? root.spec.step : 1
            decimals: 2
            value: root.value ? root.value[0] : 0
            onEdited: function (v) { root.changed(root.spec.id, [v, vy.value]) }
        }
        ThemedNumberField {
            id: vy
            Layout.fillWidth: true
            from: root.spec.min !== undefined ? root.spec.min : -10000
            to: root.spec.max !== undefined ? root.spec.max : 10000
            step: root.spec.step !== undefined ? root.spec.step : 1
            decimals: 2
            value: root.value ? root.value[1] : 0
            onEdited: function (v) { root.changed(root.spec.id, [vx.value, v]) }
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.type === "enum"
        label: root.spec.label || ""

        ThemedComboBox {
            Layout.fillWidth: true
            model: root.options
            textRole: "label"
            valueRole: "value"
            currentIndex: indexOfValue(root.value)
            onActivated: root.changed(root.spec.id, currentValue)
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.type === "color"
        label: root.spec.label || ""

        StyleColorButton {
            swatch: root.value || "white"
            onPicked: function (c) { root.changed(root.spec.id, String(c)) }
        }
    }

    ThemedCheckBox {
        Layout.fillWidth: true
        visible: root.type === "bool"
        text: root.spec.label || ""
        checked: Number(root.value) !== 0
        onClicked: root.changed(root.spec.id, checked ? 1 : 0)
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.type === "text"
        label: root.spec.label || ""

        ThemedTextField {
            id: textField
            Layout.fillWidth: true
            onTextEdited: root.changed(root.spec.id, text)
        }
        Binding {
            target: textField
            property: "text"
            when: !textField.activeFocus
            value: root.value || ""
        }
    }
}

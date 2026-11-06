import QtQuick
import QtQuick.Layouts
import Prism

// Label, slider and editable number on one row: the widget editors' SliderSpinRow.
RowLayout {
    id: root

    property string label: ""
    property real from: 0
    property real to: 1
    property real step: 0.01
    property int decimals: 2
    property string unit: ""
    property real value: 0

    signal edited(real value)

    function roundTo(v) {
        const f = Math.pow(10, root.decimals)
        return Math.round(v * f) / f
    }

    spacing: Theme.spacingSm

    ThemedLabel {
        Layout.preferredWidth: 78
        Layout.maximumWidth: 78
        text: root.label
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
    }

    ThemedSlider {
        id: slider
        Layout.fillWidth: true
        label: root.label
        from: root.from
        to: root.to
        stepSize: root.step
        value: root.value
        onMoved: root.edited(root.roundTo(slider.value))
    }

    ThemedNumberField {
        id: field
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
}

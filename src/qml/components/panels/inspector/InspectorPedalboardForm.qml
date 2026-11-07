import QtQuick
import QtQuick.Layouts
import Prism

// Audio FX: the pedal chain at a glance and the wet/dry mix. The pedals are edited in the
// pedalboard editor.
InspectorForm {
    id: root

    readonly property var chain: NodeCatalog.pedalChain(params)

    ThemedLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: root.chain.length > 0 ? root.chain.join(" › ") : qsTr("No pedals")
    }

    StyleSliderRow {
        Layout.fillWidth: true
        label: qsTr("Wet / dry")
        from: 0
        to: 100
        step: 1
        decimals: 0
        unit: "%"
        value: (root.params.wet !== undefined ? root.params.wet : 1) * 100
        onEdited: v => root.set({ wet: v / 100 }, true)
    }
}

import QtQuick
import QtQuick.Layouts
import Prism

// Parameters of the simple Process effects (Flip, Rotate, Opacity, Chroma Key, Blur, Sharpen).
// Crop, Keystone and Poly Mask need a canvas overlay and stay in ProcessEditor.
InspectorForm {
    id: root

    readonly property int effect: params.effect !== undefined ? params.effect : -1
    readonly property var values: params.params ? params.params : ({})

    function num(key, fallback) {
        const v = values[key]
        return v === undefined ? fallback : v
    }

    function setValues(patch, coalesce) {
        set({ params: Object.assign({}, values, patch) }, coalesce)
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.effect === 4
        label: qsTr("Direction")
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("Horizontal"), qsTr("Vertical"), qsTr("Both")]
            currentIndex: Math.max(0, Math.min(2, root.num("direction", 1)))
            onActivated: index => root.setValues({ direction: index })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.effect === 5
        label: qsTr("Angle")
        ThemedComboBox {
            Layout.fillWidth: true
            model: ["0°", "90°", "180°", "270°"]
            currentIndex: Math.max(0, Math.min(3, Math.round(root.num("angle", 90) / 90)))
            onActivated: index => root.setValues({ angle: index * 90 })
        }
    }

    StyleSliderRow {
        Layout.fillWidth: true
        visible: root.effect === 6
        label: qsTr("Opacity")
        from: 0
        to: 100
        step: 1
        decimals: 0
        unit: "%"
        value: root.num("opacity", 1) * 100
        onEdited: v => root.setValues({ opacity: v / 100 }, true)
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.effect === 7
        label: qsTr("Key colour")
        StyleColorButton {
            swatch: Qt.rgba(root.num("r", 0) / 255, root.num("g", 255) / 255, root.num("b", 0) / 255, 1)
            onPicked: c => root.setValues({
                r: Math.round(c.r * 255),
                g: Math.round(c.g * 255),
                b: Math.round(c.b * 255)
            })
        }
    }

    StyleSliderRow {
        Layout.fillWidth: true
        visible: root.effect === 7
        label: qsTr("Threshold")
        from: 0
        to: 100
        step: 1
        decimals: 0
        value: root.num("threshold", 0.25) * 100
        onEdited: v => root.setValues({ threshold: v / 100 }, true)
    }

    StyleSliderRow {
        Layout.fillWidth: true
        visible: root.effect === 7
        label: qsTr("Smoothness")
        from: 0
        to: 100
        step: 1
        decimals: 0
        value: root.num("smoothness", 0.1) * 100
        onEdited: v => root.setValues({ smoothness: v / 100 }, true)
    }

    StyleSliderRow {
        Layout.fillWidth: true
        visible: root.effect === 8
        label: qsTr("Radius")
        from: 0
        to: 64
        step: 1
        decimals: 0
        unit: "px"
        value: root.num("radius", 6)
        onEdited: v => root.setValues({ radius: Math.round(v) }, true)
    }

    StyleSliderRow {
        Layout.fillWidth: true
        visible: root.effect === 9
        label: qsTr("Amount")
        from: 0
        to: 3
        step: 0.01
        decimals: 2
        value: root.num("amount", 1)
        onEdited: v => root.setValues({ amount: v }, true)
    }
}

import QtQuick
import QtQuick.Layouts
import Prism

// One pedal / modulator / split control. knob is {label, min, max, def, scale, unit, options,
// value, modulated}; scale is linear | log | toggle | choice. Right-click the label to route a
// modulator to it, double-click the label to reset it.
Item {
    id: root

    property var knob: ({ label: "", min: 0, max: 1, def: 0, scale: "linear", unit: "", options: [], value: 0, modulated: false })

    signal valueEdited(real value)
    signal contextRequested()

    readonly property bool isToggle: knob.scale === "toggle"
    readonly property bool isChoice: knob.scale === "choice"
    readonly property bool integral: knob.scale === "linear" && knob.max - knob.min <= 64
                                     && Math.floor(knob.min) === knob.min && Math.floor(knob.max) === knob.max
                                     && Math.floor(knob.def) === knob.def
    property real current: knob.value

    implicitWidth: isToggle || isChoice ? simple.implicitWidth : 112
    implicitHeight: isToggle || isChoice ? simple.implicitHeight : cell.implicitHeight

    function fromNorm(t) {
        if (knob.scale === "log" && knob.min > 0)
            return knob.min * Math.pow(knob.max / knob.min, t)
        return knob.min + t * (knob.max - knob.min)
    }

    function toNorm(v) {
        const c = Math.min(knob.max, Math.max(knob.min, v))
        if (knob.max <= knob.min)
            return 0
        if (knob.scale === "log" && knob.min > 0)
            return Math.log(c / knob.min) / Math.log(knob.max / knob.min)
        return (c - knob.min) / (knob.max - knob.min)
    }

    function format(v) {
        const a = Math.abs(v)
        const prec = a >= 100 ? 0 : (a >= 10 ? 1 : 2)
        let s = v.toFixed(prec)
        if (knob.unit.length > 0)
            s += (knob.unit.charAt(0) === ":" ? "" : " ") + knob.unit
        return s
    }

    function set(v) {
        current = v
        valueEdited(v)
    }

    Loader {
        id: simple
        visible: root.isToggle || root.isChoice
        anchors.left: parent.left
        anchors.right: parent.right
        sourceComponent: root.isToggle ? toggleC : choiceC
    }

    Component {
        id: toggleC
        ThemedCheckBox {
            text: root.knob.label
            checked: root.current > 0.5
            onToggled: root.set(checked ? 1 : 0)
        }
    }

    Component {
        id: choiceC
        RowLayout {
            spacing: Theme.spacingMd
            ThemedLabel { text: root.knob.label }
            ThemedComboBox {
                Layout.fillWidth: true
                model: root.knob.options
                currentIndex: Math.max(0, Math.min(root.knob.options.length - 1, Math.round(root.current)))
                onActivated: index => root.set(index)
            }
        }
    }

    ColumnLayout {
        id: cell
        visible: !(root.isToggle || root.isChoice)
        width: parent.width
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: 3

            Rectangle {
                visible: root.knob.modulated
                Layout.preferredWidth: 6
                Layout.preferredHeight: 6
                radius: 3
                color: Theme.warning
            }
            ThemedLabel {
                Layout.fillWidth: true
                text: root.knob.label
                elide: Text.ElideRight
                wrapMode: Text.NoWrap

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onDoubleClicked: root.set(root.knob.def)
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton)
                            root.contextRequested()
                    }
                }
            }
            ThemedLabel {
                tone: "default"
                text: root.format(root.current)
                wrapMode: Text.NoWrap
            }
        }

        ThemedSlider {
            Layout.fillWidth: true
            label: root.knob.label
            showValueTooltip: false
            from: 0
            to: 1000
            value: root.toNorm(root.current) * 1000
            onMoved: {
                let v = root.fromNorm(value / 1000)
                if (root.integral)
                    v = Math.round(v)
                root.set(Math.min(root.knob.max, Math.max(root.knob.min, v)))
            }
        }
    }
}

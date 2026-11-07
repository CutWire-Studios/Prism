import QtQuick
import QtQuick.Layouts
import Prism

// Volume and mute of a node that carries audio.
InspectorForm {
    id: root

    property string volumeKey: "volume"
    property string muteKey: "muted"

    StyleSliderRow {
        Layout.fillWidth: true
        label: qsTr("Volume")
        from: 0
        to: 100
        step: 1
        decimals: 0
        unit: "%"
        value: root.params[root.volumeKey] !== undefined ? root.params[root.volumeKey] : 100
        onEdited: v => root.set({ [root.volumeKey]: Math.round(v) }, true)
        expressionName: root.volumeKey
        expression: root.expr(expressionName)
        expressionError: root.exprError(expressionName)
        onExpressionEdited: (text, integer) => root.setExpr(expressionName, text, integer)
    }

    ThemedSwitch {
        text: qsTr("Mute")
        checked: !!root.params[root.muteKey]
        onToggled: root.set({ [root.muteKey]: checked })
    }
}

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

// A column of StyleParamRow for a generated parameter list (StyleLayers.specList).
ColumnLayout {
    id: root

    property var specs: []
    property var values: ({})
    property var disabledIds: []

    signal changed(string id, var value)

    Layout.fillWidth: true
    spacing: Theme.spacingMd

    Repeater {
        model: root.specs

        delegate: StyleParamRow {
            id: row
            required property var modelData

            spec: modelData
            value: root.values[modelData.id]
            enabled: root.disabledIds.indexOf(modelData.id) < 0
            onChanged: function (id, v) { root.changed(id, v) }
        }
    }
}

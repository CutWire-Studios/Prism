import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Prism

// Parameters of the data-driven shader effects (Process effect id >= 12), built from the
// param list in NodeCatalog.processEffectParams. Generators on Input nodes override specs with
// NodeCatalog.generatorParams; their values live in the same "params" map. The Shader node, the only
// Process node with a "code" param, takes its list from the @params header of that code.
InspectorForm {
    id: root

    readonly property int effect: params.effect !== undefined ? params.effect : -1
    readonly property var values: params.params ? params.params : ({})
    property var specs: params.code !== undefined ? NodeCatalog.headerParams(params.code)
                                                  : NodeCatalog.processEffectParams(effect)

    function val(spec) {
        const v = values[spec.name]
        return v === undefined ? spec.def : v
    }

    function setValues(patch, coalesce) {
        set({ params: Object.assign({}, values, patch) }, coalesce)
    }

    function patchOf(name, value) {
        const p = {}
        p[name] = value
        return p
    }

    Repeater {
        model: root.specs

        delegate: ColumnLayout {
            id: row

            required property var modelData
            readonly property var spec: modelData
            readonly property string kind: spec.type

            Layout.fillWidth: true
            spacing: Theme.spacingSm

            StyleSliderRow {
                Layout.fillWidth: true
                visible: row.kind === "float" || row.kind === "int"
                label: row.spec.label
                from: row.spec.min
                to: row.spec.max
                step: row.spec.step
                decimals: row.kind === "int" ? 0 : Math.max(0, Math.min(4, Math.ceil(-Math.log(row.spec.step) / Math.LN10 - 1e-9)))
                value: Number(root.val(row.spec))
                onEdited: v => root.setValues(root.patchOf(row.spec.name, row.kind === "int" ? Math.round(v) : v), true)
                expressionName: "params." + row.spec.name
                expression: root.expr(expressionName)
                expressionError: root.exprError(expressionName)
                onExpressionEdited: (text, integer) => root.setExpr(expressionName, text, integer)
            }

            ThemedSwitch {
                visible: row.kind === "bool"
                text: row.spec.label
                checked: !!root.val(row.spec)
                onToggled: root.setValues(root.patchOf(row.spec.name, checked))
            }

            StyleFieldRow {
                Layout.fillWidth: true
                visible: row.kind === "choice"
                label: row.spec.label
                ThemedComboBox {
                    Layout.fillWidth: true
                    model: row.spec.choices
                    currentIndex: Math.max(0, Math.min(row.spec.choices.length - 1, Math.round(Number(root.val(row.spec)))))
                    onActivated: index => root.setValues(root.patchOf(row.spec.name, index))
                }
            }

            StyleFieldRow {
                Layout.fillWidth: true
                visible: row.kind === "color"
                label: row.spec.label
                StyleColorButton {
                    readonly property var c: row.kind === "color" ? root.val(row.spec) : [1, 1, 1, 1]
                    swatch: Qt.rgba(c[0], c[1], c[2], c[3])
                    onPicked: col => root.setValues(root.patchOf(row.spec.name, [col.r, col.g, col.b, col.a]))
                }
            }

            StyleFieldRow {
                Layout.fillWidth: true
                visible: row.kind === "file"
                label: row.spec.label

                ThemedButton {
                    Layout.fillWidth: true
                    readonly property string path: row.kind === "file" ? String(root.val(row.spec) || "") : ""
                    text: path.length > 0 ? path.split("/").pop() : qsTr("Choose file…")
                    onClicked: fileDialog.open()
                }
                ThemedButton {
                    visible: row.kind === "file" && String(root.val(row.spec) || "").length > 0
                    text: qsTr("Clear")
                    onClicked: root.setValues(root.patchOf(row.spec.name, ""))
                }

                FileDialog {
                    id: fileDialog
                    title: row.spec.label
                    nameFilters: [row.spec.filter.length > 0 ? row.spec.label + " (" + row.spec.filter + ")" : qsTr("All files (*)"),
                                  qsTr("All files (*)")]
                    onAccepted: root.setValues(root.patchOf(row.spec.name, AppController.localPath(selectedFile)))
                }
            }

            StyleFieldRow {
                id: pointRow
                Layout.fillWidth: true
                visible: row.kind === "point"
                label: row.spec.label

                readonly property var pt: row.kind === "point" ? root.val(row.spec) : [0, 0]

                ThemedNumberField {
                    Layout.fillWidth: true
                    from: -1e4
                    to: 1e4
                    step: 0.01
                    decimals: 2
                    value: pointRow.pt[0]
                    onEdited: v => root.setValues(root.patchOf(row.spec.name, [v, pointRow.pt[1]]))
                }
                ThemedNumberField {
                    Layout.fillWidth: true
                    from: -1e4
                    to: 1e4
                    step: 0.01
                    decimals: 2
                    value: pointRow.pt[1]
                    onEdited: v => root.setValues(root.patchOf(row.spec.name, [pointRow.pt[0], v]))
                }
            }
        }
    }
}

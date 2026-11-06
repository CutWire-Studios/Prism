pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// Shape source editor: the shape gallery, the geometry of the selected kind, the shading layer
// stack and a live preview. openFor(nodeId) loads the Shape node; OK writes it back to the graph.
ThemedDialog {
    id: root

    title: qsTr("Shape")
    preferredWidth: 1020
    showFooter: false
    showAccept: false
    showReject: false
    acceptOnReturn: false

    readonly property var geo: backend.geometry
    readonly property var sections: backend.sections

    ShapeSourceBackend {
        id: backend
    }

    function openFor(nodeId) {
        backend.open(AppController.graph, nodeId)
        root.open()
    }

    function commitAndClose() {
        if (backend.commit())
            root.close()
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: root.visible
        onActivated: root.commitAndClose()
    }

    contentItem: ColumnLayout {
        width: parent ? parent.width : root.preferredWidth
        spacing: Theme.spacingLg

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(root.availableContentHeight - footerRow.implicitHeight - Theme.spacingLg, 600)
            spacing: Theme.spacingLg

            ColumnLayout {
                Layout.preferredWidth: 250
                Layout.fillHeight: true
                spacing: Theme.spacingSm

                ThemedLabel {
                    text: qsTr("SHAPE")
                    font.weight: Font.DemiBold
                }
                StyleThumbGrid {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    scrollable: true
                    items: backend.kinds
                    currentId: backend.kind
                    thumbSize: Qt.size(88, 64)
                    onActivated: function (id) { backend.setKind(id) }
                }
            }

            EditorImageItem {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 240
                boxed: true
                image: backend.preview
            }

            Flickable {
                Layout.preferredWidth: 380
                Layout.fillHeight: true
                contentHeight: panelColumn.implicitHeight
                clip: true
                ScrollBar.vertical: AppScrollBar { }

                ColumnLayout {
                    id: panelColumn
                    width: parent.width
                    spacing: Theme.spacingMd

                    ThemedLabel {
                        text: qsTr("GEOMETRY")
                        font.weight: Font.DemiBold
                    }

                    StyleSliderRow {
                        Layout.fillWidth: true
                        visible: root.sections.cornerRadius === true
                        label: qsTr("Corner radius")
                        from: 0
                        to: 400
                        step: 1
                        decimals: 0
                        unit: "px"
                        value: root.geo.cornerRadius
                        onEdited: function (v) { backend.setGeometry("cornerRadius", v) }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.sections.star === true
                        spacing: Theme.spacingMd

                        StyleSliderRow {
                            Layout.fillWidth: true
                            label: qsTr("Points")
                            from: 3
                            to: 60
                            step: 1
                            decimals: 0
                            value: root.geo.points
                            onEdited: function (v) { backend.setGeometry("points", Math.round(v)) }
                        }
                        StyleSliderRow {
                            Layout.fillWidth: true
                            label: qsTr("Inner radius")
                            from: 0.05
                            to: 0.95
                            step: 0.01
                            decimals: 2
                            value: root.geo.innerRatio
                            onEdited: function (v) { backend.setGeometry("innerRatio", v) }
                        }
                    }

                    StyleSliderRow {
                        Layout.fillWidth: true
                        visible: root.sections.arrow === true
                        label: qsTr("Head size")
                        from: 0.05
                        to: 0.9
                        step: 0.01
                        decimals: 2
                        value: root.geo.headSize
                        onEdited: function (v) { backend.setGeometry("headSize", v) }
                    }

                    StyleSliderRow {
                        Layout.fillWidth: true
                        visible: root.sections.shaft === true
                        label: qsTr("Thickness")
                        from: 0.05
                        to: 1
                        step: 0.01
                        decimals: 2
                        value: root.geo.thickness
                        onEdited: function (v) { backend.setGeometry("thickness", v) }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.sections.bubble === true
                        spacing: Theme.spacingMd

                        StyleSliderRow {
                            Layout.fillWidth: true
                            label: qsTr("Tail position")
                            from: 0.08
                            to: 0.92
                            step: 0.01
                            decimals: 2
                            value: root.geo.tailX
                            onEdited: function (v) { backend.setGeometry("tailX", v) }
                        }
                        StyleSliderRow {
                            Layout.fillWidth: true
                            label: qsTr("Tail size")
                            from: 0.05
                            to: 0.5
                            step: 0.01
                            decimals: 2
                            value: root.geo.tailSize
                            onEdited: function (v) { backend.setGeometry("tailSize", v) }
                        }
                    }

                    ShadingLayerStack {
                        Layout.fillWidth: true
                        layers: backend.layers
                        onEdited: function (l) { backend.setLayers(l) }
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
                onClicked: root.commitAndClose()
            }
        }
    }
}

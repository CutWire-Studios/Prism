pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Prism

// SVG template source editor: built-in and opened SVG templates, their parameters and a live
// preview. openFor(nodeId) loads the SVG Template node; OK writes it back to the graph.
ThemedDialog {
    id: root

    title: qsTr("SVG Template")
    preferredWidth: 1000
    showFooter: false
    showAccept: false
    showReject: false
    acceptOnReturn: false

    SvgTemplateBackend {
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

    FileDialog {
        id: svgDialog
        title: qsTr("Open SVG Template")
        nameFilters: [qsTr("SVG files (*.svg)")]
        onAccepted: backend.addCustomFile(selectedFile)
    }

    contentItem: ColumnLayout {
        width: parent ? parent.width : root.preferredWidth
        spacing: Theme.spacingLg

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(root.availableContentHeight - footerRow.implicitHeight - Theme.spacingLg, 560)
            spacing: Theme.spacingLg

            ColumnLayout {
                Layout.preferredWidth: 240
                Layout.fillHeight: true
                spacing: Theme.spacingSm

                ThemedLabel {
                    text: qsTr("TEMPLATE")
                    font.weight: Font.DemiBold
                }

                ListView {
                    id: templateList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: Theme.spacingXs
                    model: backend.templates
                    ScrollBar.vertical: AppScrollBar { }

                    delegate: Rectangle {
                        id: templateRow

                        required property var modelData

                        readonly property bool selected: modelData.id === backend.templateId

                        width: templateList.width
                        height: 72
                        radius: Theme.radiusSm
                        color: selected ? Theme.panelSecondaryBg : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingSm
                            spacing: Theme.spacingMd

                            EditorImageItem {
                                Layout.preferredWidth: 88
                                Layout.preferredHeight: 50
                                image: templateRow.modelData.image
                            }
                            ThemedLabel {
                                Layout.fillWidth: true
                                size: "sm"
                                tone: "default"
                                wrapMode: Text.NoWrap
                                elide: Text.ElideRight
                                text: templateRow.modelData.label
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: backend.selectTemplate(templateRow.modelData.id)
                        }
                    }
                }

                ThemedButton {
                    Layout.fillWidth: true
                    variant: "secondary"
                    text: qsTr("Open SVG file…")
                    onClicked: svgDialog.open()
                }
            }

            EditorImageItem {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 240
                boxed: true
                image: backend.preview
            }

            ColumnLayout {
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                spacing: Theme.spacingSm

                ThemedLabel {
                    text: qsTr("PARAMETERS")
                    font.weight: Font.DemiBold
                }

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: paramColumn.implicitHeight
                    clip: true
                    ScrollBar.vertical: AppScrollBar { }

                    ColumnLayout {
                        id: paramColumn
                        width: parent.width
                        spacing: Theme.spacingMd

                        Repeater {
                            model: backend.params

                            delegate: ColumnLayout {
                                id: paramRow

                                required property var modelData

                                Layout.fillWidth: true
                                spacing: Theme.spacingXs

                                ThemedLabel {
                                    Layout.fillWidth: true
                                    text: paramRow.modelData.label
                                    tone: "default"
                                }

                                ThemedButton {
                                    id: colorButton
                                    visible: paramRow.modelData.type === "color"
                                    Layout.fillWidth: true
                                    variant: "secondary"
                                    text: paramRow.modelData.display || ""
                                    onClicked: {
                                        colorDialog.paramName = paramRow.modelData.name
                                        colorDialog.selectedColor = paramRow.modelData.swatchValid ? paramRow.modelData.swatch : "white"
                                        colorDialog.open()
                                    }
                                }

                                ThemedTextField {
                                    id: valueField
                                    visible: paramRow.modelData.type !== "color"
                                    Layout.fillWidth: true
                                    onTextEdited: backend.setParamValue(paramRow.modelData.name, text)
                                }
                                Binding {
                                    target: valueField
                                    property: "text"
                                    when: !valueField.activeFocus
                                    value: paramRow.modelData.value || ""
                                }
                            }
                        }

                        ThemedLabel {
                            Layout.fillWidth: true
                            text: qsTr("Values from a wired Script node override these at runtime. Built-in tokens: {time}, {time12}, {date}, {countdown}.")
                        }
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
                enabled: backend.templateId.length > 0
                onClicked: root.commitAndClose()
            }
        }
    }

    ThemedColorDialog {
        id: colorDialog
        property string paramName: ""
        onAccepted: backend.setParamColor(colorDialog.paramName, colorDialog.selectedColor)
    }
}

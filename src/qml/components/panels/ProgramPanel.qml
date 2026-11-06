import QtQuick
import QtQuick.Layouts
import Prism

PanelFrame {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        Item {
            id: monitorCell

            readonly property real aspect: AppController.programWidth / Math.max(1, AppController.programHeight)

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 60

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height * monitorCell.aspect)
                height: width / monitorCell.aspect
                color: "black"
                radius: Theme.radiusSm
                border.width: 1
                border.color: Theme.panelBorder
                clip: true

                PreviewItem {
                    anchors.fill: parent
                    anchors.margins: 1
                    compositor: AppController.compositor
                    source: PreviewItem.Program
                }

                ThemedLabel {
                    x: Theme.spacingSm
                    y: Theme.spacingSm
                    tone: "default"
                    font.family: Theme.monoFontFamily
                    style: Text.Outline
                    styleColor: "black"
                    text: AppController.programWidth + "x" + AppController.programHeight
                }
            }
        }

        SwitcherView {
            Layout.fillWidth: true
        }
    }
}

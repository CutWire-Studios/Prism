import QtQuick
import QtQuick.Layouts
import Prism

PanelFrame {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 60
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

        SwitcherView {
            Layout.fillWidth: true
        }
    }
}

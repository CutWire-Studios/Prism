import QtQuick
import QtQuick.Layouts
import Prism

PanelFrame {
    id: root

    GridLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        columnSpacing: Theme.spacingMd
        rowSpacing: Theme.spacingMd
        columns: width > height * 1.1 ? 2 : 1

        DeckView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            deckA: true
        }
        DeckView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            deckA: false
        }
    }
}

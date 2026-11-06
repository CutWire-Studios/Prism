import QtQuick
import QtQuick.Layouts
import Prism

// The live control strip: Staging on the left and Live on the right with the switcher between them,
// so sliding the switcher rightwards reads as moving the staged content into the Live well.
PanelFrame {
    id: root

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingLg

        DeckView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            staging: true
        }

        SwitcherView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            Layout.minimumWidth: 260
            Layout.maximumWidth: 460
        }

        DeckView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            staging: false
        }
    }
}

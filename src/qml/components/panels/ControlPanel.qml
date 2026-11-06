import QtQuick
import QtQuick.Layouts
import Prism

// The live control strip: the two decks carry equal weight with the switcher between them,
// since the operator alternates which deck is on air while prepping the other.
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
            deckA: true
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
            deckA: false
        }
    }
}

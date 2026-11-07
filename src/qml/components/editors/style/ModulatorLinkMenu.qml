pragma ComponentBehavior: Bound
import QtQuick
import Prism

// Picks the modulator that should drive a parameter: the scene's existing modulators or a new LFO.
// picked(id) carries the modulator node id, 0 for "New LFO". learnRequested() asks for a new
// Control In modulator whose OSC address is learned from the next incoming message.
ThemedContextMenu {
    id: root

    property var items: []

    signal picked(var id)
    signal learnRequested()

    onAboutToShow: items = AppController.graph.modulators()

    Repeater {
        model: root.items

        ThemedMenuItem {
            required property var modelData
            text: modelData.label
            onTriggered: root.picked(modelData.id)
        }
    }

    ThemedMenuSeparator {
        visible: root.items.length > 0
    }

    ThemedMenuItem {
        text: qsTr("New LFO")
        onTriggered: root.picked(0)
    }

    ThemedMenuItem {
        text: qsTr("Learn OSC\u2026")
        onTriggered: root.learnRequested()
    }
}

import QtQuick
import Prism

// Stand-in for a workspace panel whose implementation is not ported yet. Shows the panel's name.
PanelFrame {
    id: root

    property string panelType: ""
    property string label: ""

    ThemedLabel {
        anchors.centerIn: parent
        size: "base"
        text: root.label
    }
}

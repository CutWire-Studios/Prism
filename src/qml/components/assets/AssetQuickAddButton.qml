import QtQuick
import QtQuick.Controls.Basic
import Prism

// Round "+" that performs an asset item's primary action in one click.
Button {
    id: root

    property string tooltip: qsTr("Add to graph")
    // Cards pass their hover state; a touch UI has no hover so it is always shown.
    // Faded rather than hidden so the button stays reachable by Tab.
    property bool revealed: false

    readonly property real _visualSize: Theme.touchInput ? 30 : 24

    implicitWidth: _visualSize
    implicitHeight: _visualSize
    width: implicitWidth
    height: implicitHeight
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: root.tooltip
    Accessible.onPressAction: root.clicked()

    opacity: revealed || Theme.touchUi || visualFocus ? 1.0 : 0.0
    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    scale: !enabled ? 1.0 : down ? 0.92 : hovered ? 1.1 : 1.0
    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    background: Rectangle {
        radius: width / 2
        color: !root.enabled ? Theme.c.assetQuickAddButton.backgroundDisabled
               : root.down ? Theme.c.assetQuickAddButton.backgroundPressed
               : root.hovered ? Theme.c.assetQuickAddButton.backgroundHover
                              : Theme.c.assetQuickAddButton.background
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.c.assetQuickAddButton.focusRing
    }

    contentItem: Item {
        readonly property color crossColor: root.enabled ? Theme.c.assetQuickAddButton.foreground
                                                         : Theme.c.assetQuickAddButton.foregroundDisabled
        Rectangle {
            anchors.centerIn: parent
            width: root._visualSize / 2
            height: 2
            color: parent.crossColor
        }
        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: root._visualSize / 2
            color: parent.crossColor
        }
    }

    ThemedToolTip {
        text: root.tooltip
        visible: root.hovered || root.visualFocus
    }
}

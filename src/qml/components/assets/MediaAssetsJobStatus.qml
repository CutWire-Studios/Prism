import QtQuick
import Prism

// A one-line background job under the bin navigation: label, thin progress, cancel.
Item {
    id: root

    property string label: ""
    property real progress: 0
    property bool indeterminate: false
    property string cancelTooltip: ""

    signal cancelRequested()

    height: visible ? Math.max(statusText.implicitHeight + Theme.spacingSm + statusBar.height,
                               cancelButton.height)
                    : 0

    Text {
        id: statusText
        anchors.left: parent.left
        anchors.right: cancelButton.visible ? cancelButton.left : parent.right
        anchors.rightMargin: Theme.spacingMd
        anchors.top: parent.top
        text: root.label
        color: Theme.c.mediaAssetsJobStatus.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        elide: Text.ElideMiddle
    }
    ThemedProgressBar {
        id: statusBar
        anchors.left: parent.left
        anchors.right: cancelButton.visible ? cancelButton.left : parent.right
        anchors.rightMargin: Theme.spacingMd
        anchors.top: statusText.bottom
        anchors.topMargin: Theme.spacingSm
        barHeight: Theme.spacingXs
        value: root.indeterminate ? pulse : root.progress

        property real pulse: 0.15
        SequentialAnimation on pulse {
            running: root.indeterminate && root.visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.85; duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.15; duration: 900; easing.type: Easing.InOutSine }
        }
    }
    IconButton {
        id: cancelButton
        visible: root.cancelTooltip.length > 0
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        glyph: Theme.icons.x
        variant: "ghost"
        tooltip: root.cancelTooltip
        onClicked: root.cancelRequested()
    }
}

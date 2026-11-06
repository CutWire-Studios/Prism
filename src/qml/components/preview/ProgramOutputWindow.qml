import QtQuick
import Prism

Window {
    id: root

    property alias fillMode: preview.fillMode
    property alias source: preview.source

    signal contextMenuRequested(point globalPos)

    flags: Qt.Window | Qt.FramelessWindowHint
    color: "black"
    width: 1280
    height: 720
    title: qsTr("Program Output")

    function toggleFullscreen() {
        visibility = visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    PreviewItem {
        id: preview
        anchors.fill: parent
        compositor: AppCompositor.compositor
        source: PreviewItem.Program
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton)
                root.toggleFullscreen()
        }
        onPressed: mouse => {
            if (mouse.button === Qt.RightButton)
                root.contextMenuRequested(mapToGlobal(mouse.x, mouse.y))
            else if (root.visibility !== Window.FullScreen)
                root.startSystemMove()
        }
    }
}

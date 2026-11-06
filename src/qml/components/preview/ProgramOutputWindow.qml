import QtQuick
import Prism

Window {
    id: root

    property alias source: preview.source

    signal contextMenuRequested(point globalPos)

    flags: Qt.Window | Qt.FramelessWindowHint
    color: "black"
    width: 1280
    height: 720
    title: qsTr("Program Output")

    readonly property real programAspect: AppController.programWidth / Math.max(1, AppController.programHeight)

    function toggleFullscreen() {
        visibility = visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    // Keeps the long side so switching landscape <-> portrait doesn't shrink the window each time.
    function fitToProgram() {
        if (visibility === Window.FullScreen || visibility === Window.Maximized)
            return
        const longSide = Math.max(width, height)
        if (programAspect >= 1) {
            width = longSide
            height = Math.round(longSide / programAspect)
        } else {
            height = longSide
            width = Math.round(longSide * programAspect)
        }
    }

    Connections {
        target: AppController
        function onProgramResolutionChanged() { root.fitToProgram() }
    }

    PreviewItem {
        id: preview
        anchors.fill: parent
        compositor: AppCompositor.compositor
        source: PreviewItem.Program
        // AppController.outputScaleMode is Stretch/Fit/Crop; PreviewItem's enum is Fit/Fill/Stretch.
        fillMode: [PreviewItem.Stretch, PreviewItem.Fit, PreviewItem.Fill][AppController.outputScaleMode]
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

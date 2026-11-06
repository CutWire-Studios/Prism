import QtQuick
import Prism

Item {
    width: Theme.spacingLg + Theme.c.editorHeaderSeparator.borderWidth
    height: 32
    anchors.verticalCenter: parent.verticalCenter

    Rectangle {
        width: Theme.c.editorHeaderSeparator.borderWidth
        height: 16
        anchors.centerIn: parent
        color: Theme.c.editorHeaderSeparator.border
    }
}

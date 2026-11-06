import QtQuick
import Prism

// Folder import progress: the directory walk can take a while on a deep tree or a network mount.
Rectangle {
    id: root

    property bool active: false

    anchors.fill: parent
    z: 60
    color: Theme.c.assetsPanelImportOverlay.background
    opacity: active ? Theme.c.assetsPanelImportOverlay.scrimOpacity : 0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.active
        acceptedButtons: Qt.AllButtons
    }

    EmptyState {
        anchors.centerIn: parent
        glyph: Theme.icons.spinner
        glyphSpinning: root.active
        title: qsTr("Importing…")
        hint: qsTr("Reading the folder.")
    }
}

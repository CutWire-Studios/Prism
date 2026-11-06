import QtQuick
import Prism

ThemedDialog {
    id: root

    title: qsTr("Add NDI source")
    acceptText: qsTr("Add")
    preferredWidth: Theme.dialogWidthMd

    property var sources: []
    property bool scanned: false

    function scan() {
        sources = SourcePrompts.discoverNdi()
        scanned = true
    }

    onAboutToShow: {
        sources = []
        scanned = false
    }
    onOpened: Qt.callLater(scan)
    onAccepted: if (sourceCombo.currentIndex >= 0) SourcePrompts.addNdi(root.sources[sourceCombo.currentIndex])

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            width: parent.width
            visible: !SourcePrompts.ndiAvailable
            color: Theme.destructive
            text: qsTr("NDI is not available. Install the NDI SDK and rebuild Prism with -DNDI_ROOT=…")
        }

        ThemedLabel {
            width: parent.width
            visible: SourcePrompts.ndiAvailable && !root.scanned
            text: qsTr("Searching the network…")
        }

        ThemedLabel {
            width: parent.width
            visible: SourcePrompts.ndiAvailable && root.scanned && root.sources.length === 0
            text: qsTr("No NDI sources found on the network. Make sure another app (Prism program output, OBS, a phone NDI app) is sending NDI, then try again.")
        }

        ThemedComboBox {
            id: sourceCombo
            width: parent.width
            visible: root.sources.length > 0
            model: root.sources
        }

        ThemedButton {
            visible: root.scanned
            text: qsTr("Search again")
            onClicked: root.scan()
        }
    }
}

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Prism

ThemedDialog {
    id: root

    title: qsTr("Recording")
    showAccept: false
    rejectText: qsTr("Close")
    rejectVariant: "primary"
    preferredWidth: Theme.dialogWidthLg

    onAboutToShow: RecordingPanel.rebuild()

    Timer {
        interval: 500
        repeat: true
        running: root.visible
        onTriggered: RecordingPanel.updateStates()
    }

    contentItem: Flickable {
        id: flick
        width: parent ? parent.width : Theme.dialogWidthLg
        implicitHeight: Math.min(body.implicitHeight, root.availableContentHeight)
        contentHeight: body.implicitHeight
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: body
            width: flick.width
            spacing: Theme.spacingLg

            ThemedLabel {
                width: parent.width
                text: qsTr("Start and stop each stream independently. Tip: start the program mix and program audio together so the files are easy to match in your editor.")
            }

            ThemedLabel {
                width: parent.width
                tone: "default"
                size: "sm"
                text: qsTr("Video streams")
            }

            ThemedLabel {
                width: parent.width
                text: qsTr("Live-source rows record only while that source is loaded on Deck A or B.")
            }

            RecordingRows {
                width: parent.width
                rows: RecordingPanel.videoRows
            }

            ThemedLabel {
                width: parent.width
                tone: "default"
                size: "sm"
                text: qsTr("Audio streams")
            }

            ThemedLabel {
                width: parent.width
                text: qsTr("Saved as separate FLAC files. Program audio follows the crossfader and includes a connected Master Audio Input mic. Deck and clip isos record at full clip volume without the mic. Clip audio records only while that clip is loaded on Deck A or B.")
            }

            RecordingRows {
                width: parent.width
                audio: true
                rows: RecordingPanel.audioRows
            }

            ThemedLabel {
                width: parent.width
                tone: "default"
                size: "sm"
                text: qsTr("Output folder")
            }

            Row {
                width: parent.width
                spacing: Theme.spacingMd

                ThemedTextField {
                    id: dirField
                    width: parent.width - browse.width - parent.spacing
                    text: RecordingPanel.outputDir
                    onEditingFinished: RecordingPanel.outputDir = text
                }

                ThemedButton {
                    id: browse
                    text: qsTr("Browse…")
                    onClicked: folderDialog.open()
                }
            }
        }
    }

    Connections {
        target: RecordingPanel

        function onOutputDirChanged() {
            dirField.text = RecordingPanel.outputDir
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Select recording output folder")
        onAccepted: RecordingPanel.outputDir = decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//, ""))
    }
}

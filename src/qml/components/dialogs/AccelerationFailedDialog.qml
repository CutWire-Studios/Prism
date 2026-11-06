import QtQuick
import QtQuick.Controls.Basic
import Prism

// Shown when hardware decoding or encoding was tried and failed. The failure has already been
// handled (the clip plays in software, the recording moved to another encoder); this asks whether
// to stop trying. Opened from GpuStatus.accelerationFailed.
ThemedDialog {
    id: root

    property string stage: "decode"
    property string backend: ""
    property string errorText: ""

    readonly property bool encoding: stage === "encode"

    title: qsTr("Hardware acceleration isn't working")
    acceptText: encoding ? qsTr("Turn off hardware encoding") : qsTr("Turn off hardware decoding")
    rejectText: qsTr("Keep trying")
    preferredWidth: Theme.dialogWidthMd

    function openFor(failedStage, failedBackend, error) {
        // One question at a time; the setting it offers to change is the same for later reports.
        if (visible)
            return
        stage = failedStage
        backend = failedBackend
        errorText = error
        open()
    }

    onAccepted: {
        if (encoding)
            PreferencesController.hardwareEncode = false
        else
            PreferencesController.hardwareDecode = false
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 400

        ThemedLabel {
            width: parent.width
            wrapMode: Text.Wrap
            size: "sm"
            tone: "default"
            text: root.encoding
                  ? qsTr("\"%1\" couldn't encode the recording, so Prism used another encoder. Turn off hardware encoding to stop trying it, or keep trying if this was a one-off.").arg(root.backend)
                  : qsTr("\"%1\" couldn't decode a clip, so Prism is playing it in software. Turn off hardware decoding to stop trying it on new clips, or keep trying if this was a one-off.").arg(root.backend)
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.Wrap
            maximumLineCount: 4
            elide: Text.ElideRight
            text: root.errorText
        }
    }
}

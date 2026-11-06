import QtQuick
import Prism

ThemedDialog {
    id: root

    property bool paired: false
    property string error: ""
    property var interfaces: []

    title: qsTr("Phone camera (WebRTC)")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    acceptOnReturn: false
    property bool finished: false

    onAboutToShow: {
        paired = false
        error = ""
        interfaces = SourcePrompts.webrtcInterfaces()
        networkCombo.currentIndex = SourcePrompts.webrtcDefaultInterface()
        relayField.text = SourcePrompts.webrtcDefaultRelayUrl
    }

    function startPairing() {
        const result = SourcePrompts.webrtcStart(modeCombo.currentIndex === 1 ? relayField.text : "",
                                                 networkCombo.currentIndex)
        paired = result.ok
        error = result.ok ? "" : result.error
    }

    onClosed: {
        if (!finished)
            SourcePrompts.webrtcCancel()
        finished = false
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        Column {
            width: parent.width
            spacing: Theme.spacingLg
            visible: !root.paired

            ThemedLabel {
                width: parent.width
                visible: !SourcePrompts.webrtcAvailable
                color: Theme.destructive
                text: qsTr("WebRTC is not available in this build. Rebuild with -DPRISM_WITH_WEBRTC=ON and install qt6-websockets.")
            }

            ThemedComboBox {
                id: modeCombo
                width: parent.width
                model: [qsTr("Local network (LAN)"), qsTr("Public relay server")]
            }

            ThemedComboBox {
                id: networkCombo
                width: parent.width
                visible: modeCombo.currentIndex === 0
                model: root.interfaces
            }

            ThemedTextField {
                id: relayField
                width: parent.width
                visible: modeCombo.currentIndex === 1
                placeholderText: "wss://"
            }

            ThemedLabel {
                width: parent.width
                visible: modeCombo.currentIndex === 1
                text: qsTr("Run scripts/webrtc_signaling_server.py on your VPS, then enter its WebSocket URL here.")
            }

            ThemedLabel {
                width: parent.width
                visible: root.error.length > 0
                color: Theme.destructive
                text: root.error
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingLg
            visible: root.paired

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: SourcePrompts.webrtcQrImage
                width: 220
                height: 220
                fillMode: Image.PreserveAspectFit
                smooth: false
            }

            ThemedLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                tone: "default"
                text: SourcePrompts.webrtcPeerConnected ? qsTr("Connected") : qsTr("Waiting for phone…")
            }

            ThemedLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: SourcePrompts.webrtcUsesRelay
                      ? qsTr("Scan with your phone camera for the browser streamer, or use the Prism app. Internet pairing may require STUN/TURN for media on different networks.")
                      : qsTr("Scan with your phone camera to open the browser streamer, or scan with the Prism app to pair natively. Your phone may warn about the self-signed certificate; accept it to enable the camera.")
            }
        }

        Row {
            anchors.right: parent.right
            spacing: Theme.spacingMd

            ThemedButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }

            ThemedButton {
                variant: "primary"
                text: root.paired ? qsTr("Add") : qsTr("Start pairing")
                enabled: SourcePrompts.webrtcAvailable
                onClicked: {
                    if (root.paired) {
                        SourcePrompts.webrtcFinish()
                        root.finished = true
                        root.close()
                    } else {
                        root.startPairing()
                    }
                }
            }
        }
    }
}

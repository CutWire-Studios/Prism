pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

PanelFrame {
    id: root

    readonly property var resolutions: [
        { w: 1280, h: 720 }, { w: 1920, h: 1080 }, { w: 2560, h: 1440 }, { w: 3840, h: 2160 },
        { w: 1080, h: 1920 }, { w: 1080, h: 1080 }
    ]

    function fmtElapsed(ms) {
        const s = Math.floor(ms / 1000)
        const pad = n => (n < 10 ? "0" : "") + n
        return pad(Math.floor(s / 3600)) + ":" + pad(Math.floor(s / 60) % 60) + ":" + pad(s % 60)
    }

    function resolutionIndex() {
        for (let i = 0; i < resolutions.length; ++i) {
            if (resolutions[i].w === AppController.programWidth && resolutions[i].h === AppController.programHeight)
                return i
        }
        return -1
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: column
            width: parent.width
            spacing: Theme.spacingMd

            ThemedLabel {
                tone: "default"
                text: qsTr("Recording")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                ThemedButton {
                    text: AppController.recording ? qsTr("Stop") : qsTr("Record")
                    variant: AppController.recording ? "destructive" : "primary"
                    glyph: AppController.recording ? "square" : "circle"
                    onClicked: AppController.recording ? AppController.stopRecording() : AppController.startRecording()
                }
                ThemedLabel {
                    visible: AppController.recording
                    tone: "default"
                    font.family: Theme.monoFontFamily
                    text: root.fmtElapsed(AppController.recordingElapsedMs)
                }
                Item { Layout.fillWidth: true }
                ThemedButton {
                    visible: AppController.recording
                    text: qsTr("Marker")
                    glyph: "bookmark"
                    onClicked: AppController.dropMarker("")
                }
                IconButton {
                    glyph: "settings"
                    tooltip: qsTr("Recording settings")
                    onClicked: AppController.openRecordingSettings()
                }
            }
            ThemedLabel {
                Layout.fillWidth: true
                visible: AppController.outputDir.length > 0
                wrapMode: Text.NoWrap
                elide: Text.ElideMiddle
                text: AppController.outputDir
            }
            ThemedLabel {
                Layout.fillWidth: true
                visible: AppController.recording && AppController.recordingTracks.length > 0
                text: AppController.recordingTracks.join(", ")
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.panelBorder }

            ThemedLabel {
                tone: "default"
                text: qsTr("Program outputs")
            }
            Repeater {
                model: AppController.outputs

                delegate: RowLayout {
                    id: outRow

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        ThemedLabel {
                            tone: "default"
                            text: outRow.modelData.label
                        }
                        ThemedLabel {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: outRow.modelData.detail || (outRow.modelData.available ? "" : qsTr("Unavailable"))
                        }
                    }
                    ThemedSwitch {
                        enabled: !!outRow.modelData.available
                        checked: !!outRow.modelData.enabled
                        onToggled: {
                            if (!AppController.setOutputEnabled(outRow.modelData.id, checked))
                                checked = Qt.binding(() => !!outRow.modelData.enabled)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                ThemedButton {
                    text: qsTr("Show window")
                    glyph: "monitor"
                    onClicked: AppController.showOutputWindow()
                }
                ThemedButton {
                    text: qsTr("Hide")
                    onClicked: AppController.hideOutputWindow()
                }
                ThemedButton {
                    text: qsTr("Preview")
                    glyph: "plus"
                    tooltip: qsTr("Add a preview output window")
                    onClicked: AppController.addPreviewOutput()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                ThemedLabel { text: qsTr("Resolution") }
                ThemedComboBox {
                    Layout.fillWidth: true
                    enabled: !AppController.recording
                    model: root.resolutions.map(r => r.w + " x " + r.h)
                    currentIndex: root.resolutionIndex()
                    onActivated: index => AppController.setProgramResolution(root.resolutions[index].w,
                                                                             root.resolutions[index].h)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                ThemedLabel { text: qsTr("Scaling") }
                ThemedComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Stretch"), qsTr("Fit"), qsTr("Crop")]
                    currentIndex: AppController.outputScaleMode
                    onActivated: index => AppController.outputScaleMode = index
                }
            }
            ThemedSwitch {
                text: qsTr("Output window stays on top")
                checked: AppController.outputStayOnTop
                onToggled: AppController.outputStayOnTop = checked
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.panelBorder }

            ThemedLabel {
                tone: "default"
                text: qsTr("Remote control")
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.spacingSm
                rowSpacing: Theme.spacingSm

                ThemedButton {
                    Layout.fillWidth: true
                    text: qsTr("Remote server...")
                    glyph: "globe"
                    onClicked: AppController.openRemoteServerDialog()
                }
                ThemedButton {
                    Layout.fillWidth: true
                    text: qsTr("OSC...")
                    glyph: "radio"
                    onClicked: AppController.openOscSettings()
                }
                ThemedButton {
                    Layout.fillWidth: true
                    text: qsTr("Agent access...")
                    glyph: "bot"
                    onClicked: AppController.openAgentAccess()
                }
                ThemedButton {
                    Layout.fillWidth: true
                    text: qsTr("Hotkeys...")
                    glyph: "keyboard"
                    onClicked: AppController.openHotkeyEditor()
                }
            }
            ThemedSwitch {
                text: qsTr("MCP server")
                checked: AppController.mcpEnabled
                onToggled: AppController.mcpEnabled = checked
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.panelBorder
                visible: AppController.obsAvailable
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: AppController.obsAvailable
                spacing: Theme.spacingSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        Layout.fillWidth: true
                        tone: "default"
                        text: AppController.obsConnected ? qsTr("OBS connected") : qsTr("OBS disconnected")
                    }
                    ThemedButton {
                        text: AppController.obsConnected ? qsTr("Reconnect") : qsTr("Connect")
                        onClicked: AppController.connectObs()
                    }
                    IconButton {
                        glyph: "refresh-cw"
                        enabled: AppController.obsConnected
                        tooltip: qsTr("Refresh scenes")
                        onClicked: AppController.refreshObsScenes()
                    }
                }
                ThemedComboBox {
                    Layout.fillWidth: true
                    visible: AppController.obsConnected && AppController.obsScenes.length > 0
                    model: AppController.obsScenes
                    currentIndex: -1
                    displayText: qsTr("Switch scene")
                    onActivated: index => AppController.switchObsScene(AppController.obsScenes[index])
                }
            }
        }
    }
}

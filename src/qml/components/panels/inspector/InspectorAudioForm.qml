pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

// Mic Input / Audio Capture device picker and the Audio Analyzer analyser settings.
InspectorForm {
    id: root

    property bool isAnalyzer: false
    property bool isCapture: false

    readonly property var fftSizes: [256, 512, 1024, 2048, 4096, 8192]
    property var bands: ({})

    Timer {
        interval: 50
        repeat: true
        running: root.isAnalyzer && root.visible
        triggeredOnStart: true
        onTriggered: root.bands = AppController.analyzerBands(root.nodeId)
    }

    component OutputRow: RowLayout {
        id: outRow

        property string key
        property string hint

        readonly property real value: Number(root.bands[key] ?? 0)

        Layout.fillWidth: true
        spacing: Theme.spacingSm

        ThemedLabel {
            Layout.preferredWidth: 44
            Layout.alignment: Qt.AlignTop
            font.family: Theme.monoFontFamily
            wrapMode: Text.NoWrap
            text: outRow.key
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.spacingMd
                    radius: Theme.c.themedProgressBar.trackRadius
                    color: Theme.c.themedProgressBar.trackBackground

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, outRow.value))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.c.themedProgressBar.fillBackground
                    }
                }

                ThemedLabel {
                    Layout.preferredWidth: 36
                    horizontalAlignment: Text.AlignRight
                    font.family: Theme.monoFontFamily
                    wrapMode: Text.NoWrap
                    text: outRow.value.toFixed(2)
                }
            }

            ThemedLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.mutedForeground
                text: outRow.hint
            }
        }
    }
    readonly property var devices: isAnalyzer ? [] : isCapture ? catalog.playbackDevices() : catalog.inputDevices()

    AudioDeviceCatalog {
        id: catalog
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: !root.isAnalyzer
        label: root.isCapture ? qsTr("Playback") : qsTr("Input")
        ThemedComboBox {
            Layout.fillWidth: true
            textRole: "label"
            model: root.devices
            currentIndex: Math.max(0, root.devices.findIndex(d => d.id === (root.params.deviceId || "")))
            onActivated: index => root.set({ deviceId: root.devices[index].id, deviceLabel: root.devices[index].label })
        }
    }

    ThemedLabel {
        Layout.fillWidth: true
        visible: root.isCapture
        wrapMode: Text.WordWrap
        text: catalog.captureHint(root.params.deviceId || "")
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.isAnalyzer
        label: qsTr("FFT size")
        ThemedComboBox {
            Layout.fillWidth: true
            model: root.fftSizes
            currentIndex: Math.max(0, root.fftSizes.indexOf(root.params.fftSize))
            onActivated: index => root.set({ fftSize: root.fftSizes[index] })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.isAnalyzer
        label: qsTr("Bins")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 8
            to: 256
            value: root.params.binCount ?? 0
            onEdited: v => root.set({ binCount: Math.round(v) })
        }
    }

    StyleSliderRow {
        Layout.fillWidth: true
        visible: root.isAnalyzer
        label: qsTr("Smoothing")
        from: 0
        to: 0.98
        step: 0.01
        value: root.params.smoothing ?? 0
        onEdited: v => root.set({ smoothing: v }, true)
        expressionName: "smoothing"
        expression: root.expr(expressionName)
        expressionError: root.exprError(expressionName)
        onExpressionEdited: text => root.setExpr(expressionName, text)
    }

    StyleSliderRow {
        Layout.fillWidth: true
        visible: root.isAnalyzer
        label: qsTr("Beat")
        from: 0.1
        to: 5
        step: 0.1
        decimals: 1
        value: root.params.beatSensitivity ?? 0
        onEdited: v => root.set({ beatSensitivity: v }, true)
        expressionName: "beatSensitivity"
        expression: root.expr(expressionName)
        expressionError: root.exprError(expressionName)
        onExpressionEdited: text => root.setExpr(expressionName, text)
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.isAnalyzer
        label: qsTr("Low / mid")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 20
            to: 20000
            step: 10
            unit: "Hz"
            value: root.params.lowMidHz ?? 0
            onEdited: v => root.set({ lowMidHz: v })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.isAnalyzer
        label: qsTr("Mid / high")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 20
            to: 20000
            step: 10
            unit: "Hz"
            value: root.params.midHighHz ?? 0
            onEdited: v => root.set({ midHighHz: v })
        }
    }

    ThemedLabel {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingMd
        visible: root.isAnalyzer
        font.weight: Font.DemiBold
        text: qsTr("Outputs")
    }

    ThemedLabel {
        Layout.fillWidth: true
        visible: root.isAnalyzer
        wrapMode: Text.WordWrap
        color: Theme.mutedForeground
        text: root.bands.hasAudio === true
              ? qsTr("Every value runs from 0 to 1. Wire this node into a parameter's socket and read them in its expression as input.low, input.beat and so on.")
              : qsTr("No audio yet. Connect a clip or audio input and play it. Every value runs from 0 to 1.")
    }

    OutputRow {
        visible: root.isAnalyzer
        key: "level"
        hint: qsTr("Overall loudness: 0 is silence, 1 is very loud.")
    }
    OutputRow {
        visible: root.isAnalyzer
        key: "low"
        hint: qsTr("Bass below %1 Hz: kick drums, bass lines.").arg(root.params.lowMidHz ?? 250)
    }
    OutputRow {
        visible: root.isAnalyzer
        key: "mid"
        hint: qsTr("Mids from %1 to %2 Hz: vocals, guitars, snares.").arg(root.params.lowMidHz ?? 250).arg(root.params.midHighHz ?? 4000)
    }
    OutputRow {
        visible: root.isAnalyzer
        key: "high"
        hint: qsTr("Treble above %1 Hz: hi-hats, cymbals.").arg(root.params.midHighHz ?? 4000)
    }
    OutputRow {
        visible: root.isAnalyzer
        key: "beat"
        hint: qsTr("Jumps up on a bass hit, then falls back to 0 in about ⅓ s. Good for flashes and pulses; raise Beat to catch softer hits.")
    }

    ThemedLabel {
        Layout.fillWidth: true
        visible: root.isAnalyzer
        wrapMode: Text.WordWrap
        color: Theme.mutedForeground
        text: qsTr("low, mid and high are measured against the loudest frequency at that moment, so they show the balance of the sound rather than its volume; use level for volume. Scripts and shaders also get spectrum: %1 values from lowest to highest pitch, each 0 to 1. Smoothing steadies the values but makes them react later.").arg(root.params.binCount ?? 64)
    }
}

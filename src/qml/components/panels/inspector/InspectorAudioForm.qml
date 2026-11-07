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
}

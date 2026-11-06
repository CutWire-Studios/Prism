import QtQuick
import Prism

SettingsPage {
    id: page

    pageId: "performance"
    title: qsTr("Performance")

    SettingsGroupHeader {
        text: qsTr("Graphics")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Interface graphics API")
        description: PreferencesController.uiGraphicsApiRevertedFrom.length > 0
                     ? qsTr("%1 failed to start, so Prism fell back to OpenGL. Restart Prism to apply a different API.")
                           .arg(PreferencesController.uiGraphicsApiRevertedFrom)
                     : qsTr("Takes effect after restart. OpenGL is the default and works everywhere.")
        keywords: "renderer vulkan metal direct3d opengl gpu"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            enabled: PreferencesController.uiGraphicsApis.length > 1
            model: PreferencesController.uiGraphicsApis
            currentIndex: Math.max(0, PreferencesController.uiGraphicsApis.indexOf(PreferencesController.uiGraphicsApi))
            onActivated: PreferencesController.uiGraphicsApi = PreferencesController.uiGraphicsApis[currentIndex]
        }
    }

    SettingsRow {
        title: qsTr("Graphics card")
        description: qsTr("Which graphics card Prism runs on. High performance keeps video decoded on an NVIDIA card on that card; power saving uses less battery. Takes effect after restart.")
        keywords: "gpu integrated discrete nvidia power battery"
        available: PreferencesController.gpuPreferenceSupported
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "auto", label: qsTr("Windows default") },
                { id: "integrated", label: qsTr("Power saving (integrated GPU)") },
                { id: "discrete", label: qsTr("High performance (discrete GPU)") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === PreferencesController.gpuPreference)
                        return i
                }
                return 0
            }
            onActivated: PreferencesController.gpuPreference = model[currentIndex].id
        }
    }

    SettingsRow {
        title: qsTr("Graphics card")
        description: qsTr("Choose which graphics card Prism runs on in Windows Settings, under Display > Graphics. Takes effect after restart.")
        keywords: "gpu windows display nvidia"
        available: PreferencesController.gpuPreferenceInSystemSettings
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Open graphics settings")
            onClicked: Qt.openUrlExternally("ms-settings:display-advancedgraphics")
        }
    }

    SettingsGroupHeader {
        text: qsTr("Hardware acceleration")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Hardware decoding")
        description: qsTr("Decode source video on the graphics card when it can. Automatic keeps light clips on the CPU.")
        keywords: "decode gpu vaapi d3d11 nvdec videotoolbox codec"
        query: page.query

        ThemedSwitch {
            checked: PreferencesController.hardwareDecode
            onToggled: PreferencesController.hardwareDecode = checked
        }
    }

    SettingsRow {
        title: qsTr("Decoder")
        description: PreferencesController.decodeBackendWarning.length > 0
                     ? PreferencesController.decodeBackendWarning
                     : qsTr("Pin one decoder instead of letting Prism choose. A pinned decoder is used on its own, with no fallback to the others.")
        keywords: "decode backend nvdec vaapi d3d11 videotoolbox pin gpu"
        available: PreferencesController.decodeBackends.length > 0
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            enabled: PreferencesController.hardwareDecode
            textRole: "label"
            valueRole: "id"
            model: [{ id: "auto", label: qsTr("Automatic") }].concat(PreferencesController.decodeBackends)
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === PreferencesController.decodeBackend)
                        return i
                }
                return 0
            }
            onActivated: PreferencesController.decodeBackend = model[currentIndex].id
        }
    }

    SettingsRow {
        title: qsTr("Hardware encoding")
        description: qsTr("Record on the graphics card when it can, falling back to software if it fails. Faster, but usually a little larger for the same quality. Takes effect on the next recording.")
        keywords: "encode record gpu nvenc vaapi quick sync amf videotoolbox"
        query: page.query

        ThemedSwitch {
            checked: PreferencesController.hardwareEncode
            onToggled: PreferencesController.hardwareEncode = checked
        }
    }
}

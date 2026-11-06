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

    SettingsGroupHeader {
        text: qsTr("Hardware acceleration")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Hardware decoding")
        description: qsTr("Decode source video on the graphics card when it can. Takes effect after restart.")
        keywords: "decode gpu vaapi d3d11 codec"
        query: page.query

        ThemedSwitch {
            checked: PreferencesController.hardwareDecode
            onToggled: PreferencesController.hardwareDecode = checked
        }
    }

    SettingsRow {
        title: qsTr("Hardware encoding")
        description: qsTr("Encode exports on the graphics card when it can. Faster, but usually a little larger for the same quality.")
        keywords: "encode export gpu nvenc vaapi quick sync"
        query: page.query

        ThemedSwitch {
            checked: PreferencesController.hardwareEncode
            onToggled: PreferencesController.hardwareEncode = checked
        }
    }
}

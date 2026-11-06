import QtQuick
import Prism

SettingsPage {
    id: page

    pageId: "about"
    title: qsTr("About")

    SettingsRow {
        title: qsTr("Prism")
        description: qsTr("Version %1").arg(Qt.application.version)
        query: page.query
    }

    SettingsRow {
        title: qsTr("About Prism")
        description: qsTr("Open-source video editor by CutWire Studios.")
        keywords: "license credits version"
        query: page.query
    }
}

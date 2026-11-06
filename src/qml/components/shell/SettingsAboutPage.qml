import QtQuick
import Prism

SettingsPage {
    id: page

    pageId: "about"
    title: qsTr("About")

    SettingsRow {
        title: qsTr("Prism")
        description: qsTr("Version %1 (%2 channel)").arg(Updates.currentVersion).arg(Updates.channel)
        keywords: "update version release newer channel nightly stable"
        query: page.query
    }

    SettingsGroupHeader {
        text: qsTr("Updates")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Check for updates automatically")
        description: qsTr("Once a day, check whether a newer Prism has been released.")
        keywords: "update version release newer"
        query: page.query

        ThemedSwitch {
            checked: Updates.enabled
            onToggled: Updates.enabled = checked
        }
    }

    SettingsRow {
        title: qsTr("Check for updates")
        description: Updates.status
        keywords: "update version release newer download"
        query: page.query

        ThemedButton {
            variant: "secondary"
            glyph: Theme.icons.refresh
            text: Updates.checking ? qsTr("Checking…")
                  : Updates.downloading ? qsTr("Downloading…")
                  : qsTr("Check now")
            enabled: !Updates.checking && !Updates.downloading && !Updates.preparing
            onClicked: Updates.checkNow()
        }
    }

    SettingsRow {
        title: qsTr("About Prism")
        description: qsTr("Open-source video editor by CutWire Studios.")
        keywords: "license credits version"
        query: page.query
    }
}

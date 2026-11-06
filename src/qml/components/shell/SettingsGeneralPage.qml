import QtQuick
import Prism

SettingsPage {
    id: page

    pageId: "general"
    title: qsTr("General")

    SettingsGroupHeader {
        text: qsTr("Language & region")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Language")
        description: qsTr("Language for menus and labels. Takes effect immediately.")
        keywords: "locale translation"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: PreferencesController.uiLanguages
            currentIndex: {
                const langs = PreferencesController.uiLanguages
                for (var i = 0; i < langs.length; ++i) {
                    if (langs[i].id === PreferencesController.uiLanguage)
                        return i
                }
                return 0
            }
            onActivated: {
                const langs = PreferencesController.uiLanguages
                if (currentIndex >= 0 && currentIndex < langs.length)
                    PreferencesController.uiLanguage = langs[currentIndex].id
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Saving")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Autosave interval")
        description: qsTr("How often unsaved work is snapshotted for crash recovery.")
        keywords: "recovery backup"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: 0,   label: qsTr("Off") },
                { id: 15,  label: qsTr("Every 15 seconds") },
                { id: 30,  label: qsTr("Every 30 seconds") },
                { id: 60,  label: qsTr("Every minute") },
                { id: 300, label: qsTr("Every 5 minutes") }
            ]
            currentIndex: {
                const opts = model
                for (var i = 0; i < opts.length; ++i) {
                    if (opts[i].id === PreferencesController.autosaveIntervalSeconds)
                        return i
                }
                return 0
            }
            onActivated: {
                if (currentIndex >= 0 && currentIndex < model.length)
                    PreferencesController.autosaveIntervalSeconds = model[currentIndex].id
            }
        }
    }
}

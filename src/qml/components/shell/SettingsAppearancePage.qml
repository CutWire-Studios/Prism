import QtQuick
import Prism

SettingsPage {
    id: page

    pageId: "appearance"
    title: qsTr("Appearance")

    function chooseTheme(themeId) {
        if (themeId === "dark" || themeId === "light")
            PreferencesController.setDarkModePreference(themeId === "dark")
        if (themeId !== "system" && themeId !== "dark" && themeId !== "light") {
            const themes = ThemeManager.themes
            for (let i = 0; i < themes.length; ++i) {
                const theme = themes[i]
                if (theme.id === themeId && theme.hasOverrides && !theme.trusted) {
                    trustDialog.prompt(themeId)
                    return
                }
            }
        }
        page.applyTheme(themeId)
    }

    function applyTheme(themeId) {
        if (!ThemeManager.select(themeId))
            Toasts.error(ThemeManager.lastError || qsTr("Could not use that theme."))
        else if (ThemeManager.lastWarning)
            Toasts.warning(ThemeManager.lastWarning)
    }

    readonly property bool activeHasVariants: {
        const themes = ThemeManager.themes
        for (let i = 0; i < themes.length; ++i) {
            if (themes[i].id === ThemeManager.selection)
                return !!themes[i].hasVariants
        }
        return false
    }

    SettingsGroupHeader {
        text: qsTr("Theme")
        query: page.query
    }

    SettingsRow {
        stacked: true
        title: qsTr("Color theme")
        description: qsTr("Light and Dark follow your system. Other themes can be installed from the themes folder.")
        keywords: "dark light color"
        query: page.query

        Column {
            width: parent.width
            spacing: Theme.spacingLg

            SettingsThemeList {
                onThemeChosen: (themeId) => page.chooseTheme(themeId)
                onRevokeTrust: (themeId) => ThemeManager.setThemeTrusted(themeId, false)
            }

            ThemedLabel {
                width: parent.width
                visible: ThemeManager.appearanceFollowsTheme
                wrapMode: Text.WordWrap
                color: Theme.c.settingsRow.foreground
                text: qsTr("This theme is %1 only. Light and Dark leave it and return to the system theme.")
                      .arg(ThemeManager.effectiveVariant === "dark" ? qsTr("dark") : qsTr("light"))
            }

            ThemedLabel {
                width: parent.width
                visible: page.activeHasVariants
                wrapMode: Text.WordWrap
                color: Theme.c.settingsRow.foreground
                text: qsTr("This theme has a dark and a light variant. The header toggle switches between them.")
            }

            SettingsAppearanceActions { }
        }
    }

    SettingsRow {
        stacked: true
        title: qsTr("Translucent windows")
        description: qsTr("Lets the desktop show through panels when a theme asks for it. Takes effect after restart.")
        keywords: "transparent backdrop blur"
        query: page.query

        SettingsAppearanceTranslucency { }
    }

    // Popups are not column children. The dialog reparents itself to the overlay.
    Item {
        visible: false
        ThemeTrustDialog {
            id: trustDialog

            onTrustChosen: {
                ThemeManager.setThemeTrusted(trustDialog.themeId, true)
                page.applyTheme(trustDialog.themeId)
            }
            onSkipChosen: {
                page.applyTheme(trustDialog.themeId)
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Interface")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Interface size")
        description: PreferencesController.uiScaleNeedsRestart
                     ? qsTr("Restart Prism to apply this size.")
                     : Qt.platform.os === "windows"
                       ? qsTr("100% fits a 1080p screen, whatever your Windows display scale is.")
                       : qsTr("Makes buttons, text, and icons larger, on top of your display settings.")
        keywords: "scale zoom dpi"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: 1.0,  label: Qt.platform.os === "windows" ? "100%" : qsTr("100% (system)") },
                { id: 1.25, label: "125%" },
                { id: 1.5,  label: "150%" },
                { id: 1.75, label: "175%" },
                { id: 2.0,  label: "200%" }
            ]
            currentIndex: {
                const opts = model
                for (var i = 0; i < opts.length; ++i) {
                    if (Math.abs(opts[i].id - PreferencesController.uiScale) < 0.001)
                        return i
                }
                return 0
            }
            onActivated: {
                if (currentIndex >= 0 && currentIndex < model.length)
                    PreferencesController.uiScale = model[currentIndex].id
            }
        }
    }
}

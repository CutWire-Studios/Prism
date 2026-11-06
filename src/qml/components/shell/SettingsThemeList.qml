import QtQuick
import Prism

// System, the two built-ins, and every installed user theme.
Column {
    id: root

    width: parent ? parent.width : 0
    spacing: Theme.spacingSm

    signal themeChosen(string themeId)
    signal revokeTrust(string themeId)

    function variantText(row) {
        if (row.system)
            return qsTr("Follows light and dark")
        if (row.hasVariants)
            return qsTr("Dark and light")
        return row.variant === "light" ? qsTr("Light") : qsTr("Dark")
    }

    readonly property var rows: {
        const _revision = ThemeManager.currentTheme + ThemeManager.effectiveVariant
                           + ThemeManager.selection
        const system = [{
            id: "system",
            name: qsTr("System"),
            variant: "",
            builtIn: true,
            hasOverrides: false,
            trusted: false,
            hasVariants: false,
            system: true
        }]
        const installed = ThemeManager.themes || []
        const rest = []
        for (let i = 0; i < installed.length; ++i)
            rest.push(installed[i])
        return system.concat(rest)
    }

    Text {
        width: parent.width
        text: qsTr("Theme")
        color: Theme.c.settingsThemeList.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Medium
    }

    Repeater {
        model: root.rows
        delegate: SettingsThemeEntry {
            required property var modelData
            width: root.width
            themeId: modelData.id
            themeName: modelData.name
            variantText: root.variantText(modelData)
            hasOverrides: !!modelData.hasOverrides
            trusted: !!modelData.trusted
            hasVariants: !!modelData.hasVariants
            builtIn: !!modelData.builtIn
            systemEntry: !!modelData.system
            onActivate: root.themeChosen(themeId)
            onRevokeTrust: root.revokeTrust(themeId)
        }
    }
}

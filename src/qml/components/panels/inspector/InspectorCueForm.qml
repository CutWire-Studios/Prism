pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

// Properties of the cue selected in the cue list. Edits are one show-level undo step each.
ColumnLayout {
    id: root

    readonly property var endActions: ["hold", "loop", "autoAdvance", "black"]
    readonly property var colors: ["", "#e5484d", "#f76b15", "#ffc53d", "#30a46c", "#0090ff", "#8e4ec6"]
    property string cueId: AppController.selectedCueId
    property var info: ({})
    property string hotkeyNote: ""

    function reload() {
        info = cueId.length > 0 ? AppController.cueInfo(cueId) : ({})
    }

    function setProps(props) {
        AppController.setCueProps(cueId, props)
    }

    function transitionWith(patch) {
        const t = info.transition || { mode: 0, durationMs: 1000 }
        return Object.assign({}, t, patch)
    }

    onCueIdChanged: reload()
    Component.onCompleted: reload()

    Connections {
        target: AppController
        function onCuesEdited() { root.reload() }
        function onShowChanged() { root.reload() }
    }

    Layout.fillWidth: true
    spacing: Theme.spacingMd

    ThemedLabel {
        Layout.fillWidth: true
        tone: "default"
        size: "base"
        text: qsTr("Cue")
    }

    ThemedLabel { text: qsTr("Name") }
    ThemedTextField {
        Layout.fillWidth: true
        text: root.info.name || ""
        onEditingFinished: if (text !== root.info.name) root.setProps({ name: text })
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        ThemedLabel { text: qsTr("Color") }
        Repeater {
            model: root.colors

            delegate: Rectangle {
                id: swatch

                required property string modelData

                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                radius: 9
                color: swatch.modelData.length > 0 ? swatch.modelData : "transparent"
                border.width: (root.info.color || "") === swatch.modelData ? 2 : 1
                border.color: (root.info.color || "") === swatch.modelData ? Theme.foreground : Theme.panelBorder

                TapHandler {
                    onTapped: root.setProps({ color: swatch.modelData })
                }
            }
        }
        Item { Layout.fillWidth: true }
    }

    ThemedLabel { text: qsTr("Notes") }
    ThemedTextArea {
        Layout.fillWidth: true
        Layout.preferredHeight: 72
        text: root.info.notes || ""
        onEditingFinished: if (text !== root.info.notes) root.setProps({ notes: text })
    }

    StyleFieldRow {
        Layout.fillWidth: true
        label: qsTr("Transition in")
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("Use default")].concat(AppController.transitionModeNames)
            currentIndex: root.info.transition ? root.info.transition.mode + 1 : 0
            onActivated: index => root.setProps({
                transition: index === 0 ? null : root.transitionWith({ mode: index - 1 })
            })
        }
    }
    StyleFieldRow {
        Layout.fillWidth: true
        visible: !!root.info.transition
        label: qsTr("Duration")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 0
            to: 60
            step: 0.1
            decimals: 1
            unit: "s"
            value: root.info.transition ? root.info.transition.durationMs / 1000 : 1
            onEdited: v => root.setProps({ transition: root.transitionWith({ durationMs: Math.round(v * 1000) }) })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        label: qsTr("At end")
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("Hold"), qsTr("Loop"), qsTr("Auto-advance"), qsTr("Black")]
            currentIndex: Math.max(0, root.endActions.indexOf(root.info.endAction))
            onActivated: index => root.setProps({ endAction: root.endActions[index] })
        }
    }
    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.info.endAction === "autoAdvance" || root.info.endAction === "black"
        label: qsTr("After")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 0
            to: 86400
            step: 0.5
            decimals: 1
            unit: "s"
            value: (root.info.autoAdvanceMs || 0) / 1000
            onEdited: v => root.setProps({ autoAdvanceMs: Math.round(v * 1000) })
        }
    }
    ThemedSwitch {
        visible: root.info.endAction === "autoAdvance" || root.info.endAction === "black"
        text: qsTr("When media ends")
        checked: !!root.info.advanceOnMediaEnd
        onToggled: root.setProps({ advanceOnMediaEnd: checked })
    }

    StyleFieldRow {
        Layout.fillWidth: true
        label: qsTr("Hotkey")
        ShortcutCaptureField {
            actionId: root.info.name || ""
            shortcut: root.info.hotkey || ""
            onCaptured: chord => root.hotkeyNote = AppController.setCueHotkey(root.cueId, chord)
            onCleared: root.hotkeyNote = AppController.setCueHotkey(root.cueId, "")
        }
    }
    ThemedLabel {
        Layout.fillWidth: true
        visible: root.hotkeyNote.length > 0
        tone: "destructive"
        wrapMode: Text.WordWrap
        text: root.hotkeyNote
    }
    ThemedLabel {
        Layout.fillWidth: true
        visible: !!root.info.warning
        wrapMode: Text.WordWrap
        color: Theme.destructive
        text: root.info.warning || ""
    }
}

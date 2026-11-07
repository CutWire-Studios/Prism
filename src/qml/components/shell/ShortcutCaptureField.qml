import QtQuick
import QtQuick.Controls.Basic
import Prism

// Click or focus and press Space/Enter to arm, then press a chord (modifiers + key).
// Esc cancels; Backspace clears. The owner stores the result: `captured` carries the chord,
// `cleared` an empty binding.
//
// Built on AbstractButton rather than a bare Rectangle so it joins the tab chain and
// reports a role — as a plain Rectangle it was unreachable by keyboard, which made the
// keyboard-settings panel itself impossible to use from the keyboard.
AbstractButton {
    id: root

    property string actionId
    property string shortcut
    property bool capturing: false
    // Read by HotkeyManager: every key belongs to the field while armed, so scene hotkeys stand aside.
    readonly property bool hotkeyCaptureAll: capturing

    signal captured(string chord)
    signal cleared()

    implicitWidth: 120
    implicitHeight: 26
    width: 120
    height: 26
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Shortcut for %1").arg(root.actionId)
    Accessible.description: root.shortcut.length > 0 ? Theme.shortcutDisplay(root.shortcut) : qsTr("Not set")
    Accessible.onPressAction: root.arm()

    function arm() {
        root.capturing = true
        root.forceActiveFocus()
    }

    onClicked: root.arm()

    background: Rectangle {
        radius: Theme.c.shortcutCaptureField.radius
        color: root.capturing ? Theme.c.shortcutCaptureField.backgroundCapturing
                              : (root.hovered ? Theme.c.shortcutCaptureField.backgroundHover
                                             : Theme.c.shortcutCaptureField.background)
        border.width: root.capturing || root.visualFocus ? Theme.c.shortcutCaptureField.borderWidthFocus
                                                         : Theme.c.shortcutCaptureField.borderWidth
        border.color: root.capturing ? Theme.c.shortcutCaptureField.borderCapturing
                                     : (root.visualFocus ? Theme.c.shortcutCaptureField.borderFocus
                                                         : Theme.c.shortcutCaptureField.border)

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    function keyName(key) {
        if (key >= Qt.Key_A && key <= Qt.Key_Z)
            return String.fromCharCode(key)
        if (key >= Qt.Key_0 && key <= Qt.Key_9)
            return String.fromCharCode(key)
        if (key >= Qt.Key_F1 && key <= Qt.Key_F12)
            return "F" + (key - Qt.Key_F1 + 1)
        const named = {
            [Qt.Key_Space]: "Space", [Qt.Key_Tab]: "Tab", [Qt.Key_Return]: "Return",
            [Qt.Key_Enter]: "Enter", [Qt.Key_Home]: "Home", [Qt.Key_End]: "End",
            [Qt.Key_PageUp]: "PageUp", [Qt.Key_PageDown]: "PageDown", [Qt.Key_Left]: "Left",
            [Qt.Key_Right]: "Right", [Qt.Key_Up]: "Up", [Qt.Key_Down]: "Down",
            [Qt.Key_Comma]: ",", [Qt.Key_Period]: ".", [Qt.Key_Slash]: "/"
        }
        return named[key] ?? ""
    }

    // "Ctrl+Shift+K" style, the notation Theme.shortcutDisplay reads. Empty for a bare modifier.
    function chordFromEvent(event) {
        const key = root.keyName(event.key)
        if (key === "")
            return ""
        let chord = ""
        if (event.modifiers & Qt.ControlModifier)
            chord += "Ctrl+"
        if (event.modifiers & Qt.AltModifier)
            chord += "Alt+"
        if (event.modifiers & Qt.ShiftModifier)
            chord += "Shift+"
        if (event.modifiers & Qt.MetaModifier)
            chord += "Meta+"
        return chord + key
    }

    contentItem: Text {
        anchors.margins: 4
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: root.capturing ? qsTr("Press keys…")
                             : (root.shortcut.length > 0
                                ? Theme.shortcutDisplay(root.shortcut)
                                : qsTr("Click to set"))
        color: root.capturing ? Theme.c.shortcutCaptureField.foregroundCapturing
                              : Theme.c.shortcutCaptureField.foreground
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Keys.onPressed: function(event) {
        // Space/Enter arm the field for keyboard users; AbstractButton would
        // otherwise consume them as an activation while already armed.
        if (!root.capturing) {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                    || event.key === Qt.Key_Enter) {
                root.arm()
                event.accepted = true
            }
            return
        }

        if (event.key === Qt.Key_Escape) {
            root.capturing = false
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
            root.cleared()
            root.capturing = false
            event.accepted = true
            return
        }

        const chord = root.chordFromEvent(event)
        if (chord.length === 0)
            return

        root.captured(chord)
        root.capturing = false
        event.accepted = true
    }

    onActiveFocusChanged: {
        if (!activeFocus && root.capturing)
            root.capturing = false
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: Qt.PointingHandCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
}

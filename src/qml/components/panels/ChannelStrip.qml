import QtQuick
import QtQuick.Layouts
import Prism

RowLayout {
    id: root

    property string name: ""
    property int volume: 100
    property bool muted: false

    signal volumeCommitted(int volume)
    signal muteToggled(bool muted)

    spacing: Theme.spacingSm

    ThemedLabel {
        Layout.preferredWidth: 110
        tone: "default"
        elide: Text.ElideRight
        wrapMode: Text.NoWrap
        text: root.name
    }
    ThemedSlider {
        id: slider
        Layout.fillWidth: true
        label: root.name
        from: 0
        to: 100
        stepSize: 1
        opacity: root.muted ? 0.4 : 1
        valueFormatter: v => Math.round(v) + "%"
        onPressedChanged: {
            if (!pressed)
                root.volumeCommitted(Math.round(value))
        }
        onMoved: {
            if (!pressed)
                root.volumeCommitted(Math.round(value))
        }
        Binding {
            target: slider
            property: "value"
            value: root.volume
            when: !slider.pressed
        }
    }
    ThemedLabel {
        Layout.preferredWidth: 36
        horizontalAlignment: Text.AlignRight
        font.family: Theme.monoFontFamily
        text: Math.round(slider.value) + "%"
    }
    IconButton {
        glyph: root.muted ? "volume-x" : "volume-2"
        active: root.muted
        tooltip: root.muted ? qsTr("Unmute") : qsTr("Mute")
        onClicked: root.muteToggled(!root.muted)
    }
}

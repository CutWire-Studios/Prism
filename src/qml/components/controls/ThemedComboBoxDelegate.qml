import QtQuick
import QtQuick.Controls.Basic
import Prism

ItemDelegate {
    id: comboDelegate
    required property int index
    required property var model
    required property var modelData
    required property ComboBox combo
    width: combo.width
    height: Theme.iconButtonSize
    highlighted: combo.highlightedIndex === index
    // ItemDelegate defaults to hoverEnabled: false in Controls.Basic, so the
    // hover fill below never rendered.
    hoverEnabled: true
    // Objects may expose `available: false` (export codecs) to gray out rows.
    enabled: {
        if (modelData && modelData.available !== undefined)
            return !!modelData.available
        if (model && model.available !== undefined)
            return !!model.available
        return true
    }

    // Objects may expose `warn: true` (a decoder that runs on the wrong GPU) to flag a row
    // that works but costs something. Unlike `available` it leaves the row selectable.
    readonly property bool warned: (modelData && modelData.warn === true)
                                   || (model && model.warn === true)

    contentItem: Text {
        // modelData is undefined for multi-role item models (VoiceLibrary), so guard it.
        text: combo.textRole ? (modelData && modelData[combo.textRole] !== undefined
                               ? modelData[combo.textRole]
                               : (model[combo.textRole] !== undefined ? model[combo.textRole] : modelData))
                            : (combo.labels ? (combo.labels[modelData] || modelData) : modelData)
        color: Theme.c.themedComboBoxDelegate.foreground
        opacity: comboDelegate.enabled ? 1 : 0.4
        font: combo.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        leftPadding: Theme.spacingLg
        rightPadding: comboDelegate.warned ? Theme.iconSizeSm + Theme.spacingLg * 2
                                           : Theme.spacingLg
    }

    IconGlyph {
        visible: comboDelegate.warned
        glyph: Theme.icons.warning
        iconSize: Theme.iconSizeSm
        iconColor: Theme.c.themedComboBoxDelegate.icon
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
    }

    background: Rectangle {
        color: comboDelegate.highlighted ? Theme.c.themedComboBoxDelegate.backgroundHighlighted
                                         : (comboDelegate.hovered ? Theme.c.themedComboBoxDelegate.backgroundHover : "transparent")

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: comboDelegate.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
    }

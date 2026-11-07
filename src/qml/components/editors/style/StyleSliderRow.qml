import QtQuick
import QtQuick.Layouts
import Prism

// Label, slider and editable number on one row: the widget editors' SliderSpinRow.
// Setting expressionName adds a small "f" toggle that swaps the slider for a chip showing the
// expression's source and result; clicking it opens the expression editor popup. The owner
// stores the text from expressionEdited ("" removes the expression and its node socket) and
// whether results must be whole numbers.
RowLayout {
    id: root

    property string label: ""
    property real from: 0
    property real to: 1
    property real step: 0.01
    property int decimals: 2
    property string unit: ""
    property real value: 0
    property string expressionName: ""
    property string expression: ""
    property string expressionError: ""
    // Raw parameter units per displayed unit; a new modulator is scaled to from*exprScale..to*exprScale.
    property real exprScale: 1
    property var linkForm: null
    // Id of the Control In modulator waiting for an OSC message to learn; 0 when idle.
    property var learnId: 0
    readonly property bool learning: learnId !== 0
    readonly property bool exprMode: expressionName.length > 0 && expression.length > 0

    signal edited(real value)
    // Whole-numbered raw values: no decimals shown and no display scale (percent sliders show
    // whole numbers but store 0..1).
    readonly property bool integerValue: decimals === 0 && exprScale === 1

    signal expressionEdited(string expression, bool integer)

    readonly property var paramSource: {
        if (!linkForm || expressionName.length === 0 || expression.length === 0)
            return ({ connected: false })
        void linkForm.graph.revision
        return linkForm.graph.paramSource(linkForm.nodeId, expressionName)
    }

    function roundTo(v) {
        const f = Math.pow(10, root.decimals)
        return Math.round(v * f) / f
    }

    spacing: Theme.spacingSm

    Component.onCompleted: {
        let p = root.parent
        while (p && p.bindModulator === undefined)
            p = p.parent
        linkForm = p
    }

    ThemedLabel {
        Layout.preferredWidth: 78
        Layout.maximumWidth: 78
        text: root.label
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
    }

    ThemedSlider {
        id: slider
        visible: !root.exprMode
        Layout.fillWidth: true
        label: root.label
        from: root.from
        to: root.to
        stepSize: root.step
        value: root.value
        onMoved: root.edited(root.roundTo(slider.value))
    }

    ThemedNumberField {
        id: field
        visible: !root.exprMode
        Layout.preferredWidth: 78
        from: root.from
        to: root.to
        step: root.step
        decimals: root.decimals
        value: root.value
        onEdited: function (v) { root.edited(root.roundTo(v)) }
    }

    ThemedLabel {
        visible: root.unit.length > 0
        text: root.unit
    }

    Rectangle {
        visible: root.learning
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.controlHeightSm
        radius: height / 2
        color: Theme.panelSecondaryBg
        border.color: Theme.panelBorder

        ThemedLabel {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            anchors.right: cancelLearnButton.left
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
            text: qsTr("Waiting for OSC\u2026")
        }

        IconButton {
            id: cancelLearnButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            glyph: "unlink-2"
            tooltip: qsTr("Cancel OSC learn")
            buttonSize: Theme.controlHeightSm
            onClicked: {
                AppController.cancelOscLearn()
                root.learnId = 0
                root.expressionEdited("", root.integerValue)
            }
        }
    }

    Rectangle {
        id: chip
        visible: root.exprMode && !root.learning
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.controlHeightSm
        radius: Theme.radiusSm
        color: chipMouse.containsMouse ? Theme.panelAccent : Theme.panelSecondaryBg
        border.width: 1
        border.color: root.expressionError.length > 0 ? Theme.destructive : Theme.panelBorder

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMd
            anchors.rightMargin: Theme.spacingMd
            spacing: Theme.spacingSm

            ThemedLabel {
                Layout.maximumWidth: chip.width * 0.35
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                color: root.paramSource.connected ? Theme.foreground : Theme.warning
                text: root.paramSource.connected ? root.paramSource.label : qsTr("Not connected")
            }

            ThemedLabel {
                Layout.fillWidth: true
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                color: root.expressionError.length > 0 ? Theme.destructive : Theme.mutedForeground
                font.family: Theme.monoFontFamily
                text: root.expression
            }

            ThemedLabel {
                wrapMode: Text.NoWrap
                text: root.value.toFixed(root.decimals) + (root.unit.length > 0 ? " " + root.unit : "")
            }
        }

        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: editorPopup.open()
        }

        ThemedToolTip {
            text: root.expressionError.length > 0 ? root.expressionError : qsTr("Edit expression")
            visible: chipMouse.containsMouse
        }
    }

    IconButton {
        visible: root.linkForm !== null && root.expressionName.length > 0
                 && !root.paramSource.connected && !root.learning
        glyph: "link-2"
        tooltip: qsTr("Link to a modulator")
        buttonSize: Theme.controlHeightSm
        onClicked: linkMenu.popup()
    }

    ModulatorLinkMenu {
        id: linkMenu
        onPicked: id => root.linkForm.bindModulator(root.expressionName, id,
                                                    root.from * root.exprScale, root.to * root.exprScale,
                                                    0, root.integerValue)
        onLearnRequested: {
            const id = root.linkForm.bindModulator(root.expressionName, 0,
                                                   root.from * root.exprScale, root.to * root.exprScale, 9,
                                                   root.integerValue)
            if (id) {
                root.learnId = id
                AppController.startOscLearn()
            }
        }
    }

    ExpressionEditorPopup {
        id: editorPopup
        label: root.label
        nodeId: root.linkForm ? root.linkForm.nodeId : 0
        paramName: root.expressionName
        expression: root.expression
        expressionError: root.expressionError
        sourceLabel: root.paramSource.label || ""
        connected: root.paramSource.connected === true
        sourceType: root.paramSource.type ?? -1
        from: root.from
        to: root.to
        exprScale: root.exprScale
        integer: root.integerValue
        unit: root.unit
        linkForm: root.linkForm
        onApplied: text => root.expressionEdited(text, root.integerValue)
        onLinkRequested: {
            close()
            linkMenu.popup()
        }
    }

    Connections {
        target: AppController
        enabled: root.learning

        function onOscLearned(address) {
            root.linkForm.setModulatorInput(root.learnId, "osc:" + address)
            root.learnId = 0
        }

        function onOscLearnTimedOut() {
            root.learnId = 0
        }
    }

    Component.onDestruction: {
        if (root.learning)
            AppController.cancelOscLearn()
    }

    ThemedToggleButton {
        visible: root.expressionName.length > 0
        text: "\u0192"
        tooltip: qsTr("Drive with an expression \u2014 adds a socket on the node")
        toggleHeight: Theme.controlHeightSm
        checkable: false
        checked: root.exprMode
        onClicked: {
            if (root.exprMode) {
                root.expressionEdited("", root.integerValue)
            } else {
                root.expressionEdited("input", root.integerValue)
                editorPopup.open()
            }
        }
    }
}

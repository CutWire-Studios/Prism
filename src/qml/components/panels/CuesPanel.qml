pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// The show's cue list. Click stages a cue, double-click stages and takes it, the pencil enters
// the cue to edit its graph. The breadcrumb leaves the cue again.
PanelFrame {
    id: root

    readonly property var endActions: ["hold", "loop", "autoAdvance", "black"]
    property var info: ({})

    function reload() {
        info = AppController.editedCueId.length > 0 ? AppController.cueInfo(AppController.editedCueId) : ({})
    }

    function setProps(props) {
        AppController.setCueProps(root.info.id, props)
        root.reload()
    }

    function transitionWith(patch) {
        const t = root.info.transition || { mode: 0, durationMs: 1000 }
        return Object.assign({}, t, patch)
    }

    Component.onCompleted: reload()

    Connections {
        target: AppController
        function onShowChanged() { root.reload() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            IconButton {
                glyph: "chevron-left"
                tooltip: qsTr("Back to show")
                visible: AppController.inCue
                onClicked: AppController.exitCue()
            }
            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                elide: Text.ElideRight
                text: AppController.inCue ? qsTr("Show / %1").arg(root.info.name || "") : qsTr("Show")
            }
            IconButton {
                glyph: "plus"
                tooltip: qsTr("Add cue")
                onClicked: AppController.addCue()
            }
        }

        ListView {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: AppController.cues
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            delegate: Rectangle {
                id: row

                required property int index
                required property string cueId
                required property string name
                required property bool isLive
                required property bool isStaging
                required property bool isEdited
                required property bool hasTransition

                width: ListView.view.width
                height: Theme.controlHeight + Theme.spacingSm
                radius: Theme.radiusSm
                color: isEdited ? Theme.panelSecondaryBg : "transparent"
                border.color: isStaging || isLive ? Theme.primary : Theme.panelBorder
                border.width: 1

                TapHandler {
                    onTapped: {
                        if (tapCount === 2)
                            AppController.stageAndTake(row.cueId)
                        else
                            AppController.stageCue(row.cueId)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMd
                    anchors.rightMargin: Theme.spacingSm
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        text: row.index + 1
                    }
                    ThemedLabel {
                        Layout.fillWidth: true
                        tone: "default"
                        elide: Text.ElideRight
                        text: row.name
                    }
                    ThemedLabel {
                        visible: row.hasTransition
                        text: qsTr("T")
                    }
                    Rectangle {
                        visible: row.isStaging
                        implicitWidth: stagingText.implicitWidth + Theme.spacingMd
                        implicitHeight: stagingText.implicitHeight + 2
                        radius: Theme.radiusSm
                        color: Theme.panelSecondaryBg
                        ThemedLabel {
                            id: stagingText
                            anchors.centerIn: parent
                            tone: "default"
                            text: qsTr("STAGING")
                        }
                    }
                    Rectangle {
                        visible: row.isLive
                        implicitWidth: liveText.implicitWidth + Theme.spacingMd
                        implicitHeight: liveText.implicitHeight + 2
                        radius: Theme.radiusSm
                        color: Theme.destructive
                        ThemedLabel {
                            id: liveText
                            anchors.centerIn: parent
                            tone: "default"
                            text: qsTr("LIVE")
                        }
                    }
                    IconButton {
                        glyph: "pencil"
                        tooltip: qsTr("Edit cue graph")
                        onClicked: AppController.enterCue(row.cueId)
                    }
                    IconButton {
                        glyph: "copy-plus"
                        tooltip: qsTr("Duplicate cue")
                        onClicked: AppController.duplicateCue(row.cueId)
                    }
                    IconButton {
                        glyph: "trash-2"
                        tooltip: qsTr("Delete cue")
                        enabled: !row.isLive && list.count > 1
                        onClicked: AppController.removeCue(row.cueId)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: AppController.inCue && root.info.id !== undefined
            spacing: Theme.spacingSm

            StyleFieldRow {
                Layout.fillWidth: true
                label: qsTr("Name")
                ThemedTextField {
                    Layout.fillWidth: true
                    text: root.info.name || ""
                    onEditingFinished: if (text !== root.info.name) root.setProps({ name: text })
                }
            }
            StyleFieldRow {
                Layout.fillWidth: true
                label: qsTr("Transition")
                ThemedComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Show default")].concat(AppController.transitionModeNames)
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
        }
    }
}

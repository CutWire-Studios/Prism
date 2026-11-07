pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// The show's cue list. Click selects and stages a cue, double-click takes it, the pencil enters
// the cue to edit its graph, the grip drags it to a new position. The selected cue's properties
// live in the inspector. While the list has focus: Up/Down select, Enter stages, Space takes,
// Ctrl+D duplicates, Delete removes, Ctrl+Z undoes the last list edit.
PanelFrame {
    id: root

    readonly property var modeNames: AppController.transitionModeNames
    property var chipInfo: ({})
    property string editedName: ""

    function reloadName() {
        editedName = AppController.editedCueId.length > 0 ? AppController.cueInfo(AppController.editedCueId).name : ""
    }

    Component.onCompleted: reloadName()

    function chipText(mode, ms) {
        return mode < 0 ? qsTr("default") : qsTr("%1 %2 s").arg(root.modeNames[mode] || "").arg((ms / 1000).toFixed(1))
    }

    function openChip(cueId, anchor) {
        chipPopup.cueId = cueId
        root.reloadChip()
        chipPopup.parent = anchor
        chipPopup.open()
    }

    function reloadChip() {
        chipInfo = chipPopup.cueId.length > 0 ? AppController.cueInfo(chipPopup.cueId) : ({})
    }

    function chipTransition(patch) {
        const t = chipInfo.transition || { mode: 0, durationMs: 1000 }
        return Object.assign({}, t, patch)
    }

    Connections {
        target: AppController
        function onCuesEdited() {
            root.reloadName()
            if (chipPopup.visible)
                root.reloadChip()
        }
        function onShowChanged() { root.reloadName() }
    }

    Popup {
        id: chipPopup

        property string cueId: ""

        padding: Theme.spacingMd
        width: 240
        y: parent ? parent.height : 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.panelSecondaryBg
            border.color: Theme.panelBorder
            radius: Theme.radiusSm
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingSm

            ThemedLabel {
                tone: "default"
                text: qsTr("Transition into this cue")
            }
            ThemedComboBox {
                Layout.fillWidth: true
                model: [qsTr("Use default")].concat(root.modeNames)
                currentIndex: root.chipInfo.transition ? root.chipInfo.transition.mode + 1 : 0
                onActivated: index => AppController.setCueProps(chipPopup.cueId, {
                    transition: index === 0 ? null : root.chipTransition({ mode: index - 1 })
                })
            }
            ThemedNumberField {
                Layout.fillWidth: true
                visible: !!root.chipInfo.transition
                from: 0
                to: 60
                step: 0.1
                decimals: 1
                unit: "s"
                value: root.chipInfo.transition ? root.chipInfo.transition.durationMs / 1000 : 1
                onEdited: v => AppController.setCueProps(chipPopup.cueId, {
                    transition: root.chipTransition({ durationMs: Math.round(v * 1000) })
                })
            }
        }
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
                text: AppController.inCue
                      ? qsTr("Show / %1").arg(root.editedName)
                      : qsTr("Show")
            }
            IconButton {
                glyph: "triangle-alert"
                tooltip: qsTr("Preflight check")
                onClicked: AppController.openPreflight()
            }
            IconButton {
                glyph: "plus"
                tooltip: qsTr("Add cue")
                onClicked: AppController.addCue()
            }
        }

        ListView {
            id: list

            readonly property bool cueListFocus: true
            property int dragFrom: -1
            property int dropIndex: -1

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 0
            model: AppController.cues
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            function select(i) {
                if (i < 0 || i >= count)
                    return
                currentIndex = i
                positionViewAtIndex(i, ListView.Contain)
            }

            Keys.onUpPressed: select(currentIndex - 1)
            Keys.onDownPressed: select(currentIndex + 1)
            Keys.onReturnPressed: AppController.stageCue(AppController.selectedCueId)
            Keys.onEnterPressed: AppController.stageCue(AppController.selectedCueId)
            Keys.onSpacePressed: AppController.take()
            Keys.onDeletePressed: AppController.deleteCue(AppController.selectedCueId)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_D && (event.modifiers & Qt.ControlModifier)) {
                    AppController.duplicateCue(AppController.selectedCueId)
                    event.accepted = true
                }
            }

            delegate: Item {
                id: row

                required property int index
                required property string cueId
                required property string name
                required property string color
                required property bool isLive
                required property bool isStaging
                required property bool isEdited
                required property int transitionMode
                required property int transitionMs
                required property string thumbnail
                required property string hotkey
                required property string warning

                readonly property bool selected: cueId === AppController.selectedCueId

                width: ListView.view.width
                height: (index > 0 ? chip.height : 0) + card.height

                ListView.onIsCurrentItemChanged: if (ListView.isCurrentItem && list.activeFocus)
                    AppController.selectedCueId = cueId
                onSelectedChanged: if (selected) list.currentIndex = index
                Component.onCompleted: if (selected) list.currentIndex = index

                Rectangle {
                    id: chip

                    visible: row.index > 0
                    height: visible ? 20 : 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    implicitWidth: chipLabel.implicitWidth + 2 * Theme.spacingMd
                    width: implicitWidth
                    radius: height / 2
                    color: chipTap.pressed ? Theme.panelSecondaryBg : "transparent"

                    ThemedLabel {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: root.chipText(row.transitionMode, row.transitionMs)
                        color: row.transitionMode < 0 ? Theme.mutedForeground : Theme.foreground
                    }
                    TapHandler {
                        id: chipTap
                        onTapped: root.openChip(row.cueId, chip)
                    }
                }

                Rectangle {
                    id: card

                    anchors.top: parent.top
                    anchors.topMargin: row.index > 0 ? chip.height : 0
                    width: parent.width
                    height: 52
                    radius: Theme.radiusSm
                    color: row.selected ? Theme.panelSecondaryBg : "transparent"
                    border.color: row.isLive ? Theme.destructive
                                  : row.isStaging ? Theme.primary
                                  : row.selected ? Theme.mutedForeground : Theme.panelBorder
                    border.width: 1
                    opacity: list.dragFrom === row.index ? 0.5 : 1

                    TapHandler {
                        onTapped: {
                            list.forceActiveFocus()
                            AppController.selectedCueId = row.cueId
                            if (tapCount === 2)
                                AppController.stageAndTake(row.cueId)
                            else
                                AppController.stageCue(row.cueId)
                        }
                    }

                    Rectangle {
                        visible: row.color.length > 0
                        width: 3
                        height: parent.height - 8
                        x: 2
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 1
                        color: row.color
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMd
                        anchors.rightMargin: Theme.spacingSm
                        spacing: Theme.spacingSm

                        IconGlyph {
                            id: grip
                            glyph: "grip-vertical"
                            iconColor: Theme.mutedForeground
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: Qt.OpenHandCursor
                                preventStealing: true
                                onPressed: list.dragFrom = row.index
                                onPositionChanged: mouse => {
                                    const p = mapToItem(list.contentItem, mouse.x, mouse.y)
                                    const to = list.indexAt(10, p.y)
                                    if (to >= 0)
                                        list.dropIndex = to
                                }
                                onReleased: {
                                    const to = list.dropIndex
                                    const from = list.dragFrom
                                    list.dragFrom = -1
                                    list.dropIndex = -1
                                    if (to >= 0 && to !== from)
                                        AppController.moveCue(row.cueId, to)
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 64
                            Layout.preferredHeight: 36
                            color: Theme.sliderTrack
                            radius: 2
                            clip: true

                            Image {
                                anchors.fill: parent
                                visible: row.thumbnail.length > 0
                                source: row.thumbnail
                                fillMode: Image.PreserveAspectFit
                                cache: false
                            }
                            IconGlyph {
                                anchors.centerIn: parent
                                visible: row.thumbnail.length === 0
                                glyph: "film"
                                iconColor: Theme.mutedForeground
                            }
                        }

                        ThemedLabel {
                            text: row.index + 1
                        }
                        ThemedLabel {
                            Layout.fillWidth: true
                            tone: "default"
                            elide: Text.ElideRight
                            text: row.name
                        }

                        IconGlyph {
                            visible: row.warning.length > 0
                            glyph: "triangle-alert"
                            iconColor: Theme.destructive
                            HoverHandler { id: warnHover }
                            ToolTip.visible: warnHover.hovered
                            ToolTip.text: row.warning
                        }
                        Rectangle {
                            visible: row.hotkey.length > 0
                            implicitWidth: keyText.implicitWidth + Theme.spacingMd
                            implicitHeight: keyText.implicitHeight + 2
                            radius: Theme.radiusSm
                            color: "transparent"
                            border.color: Theme.panelBorder
                            ThemedLabel {
                                id: keyText
                                anchors.centerIn: parent
                                text: row.hotkey
                            }
                        }
                        Rectangle {
                            visible: row.isStaging
                            implicitWidth: stagingText.implicitWidth + Theme.spacingMd
                            implicitHeight: stagingText.implicitHeight + 2
                            radius: Theme.radiusSm
                            color: Theme.primary
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
                            onClicked: AppController.deleteCue(row.cueId)
                        }
                    }

                    Rectangle {
                        visible: list.dragFrom >= 0 && list.dropIndex === row.index && list.dropIndex !== list.dragFrom
                        width: parent.width
                        height: 2
                        color: Theme.primary
                        y: list.dropIndex < list.dragFrom ? 0 : parent.height - 2
                    }
                }
            }
        }
    }
}

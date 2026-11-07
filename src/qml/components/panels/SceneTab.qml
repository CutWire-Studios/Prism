pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// One scene in the tab bar. Click opens it for editing (never changes Live or Staging),
// double-click renames, middle-click closes, right-click opens the menu, drag reorders.
// An Item root: the `color` model role would otherwise shadow Rectangle.color.
Item {
    id: root

    required property int index
    required property string sceneId
    required property string name
    required property string color
    required property bool isLive
    required property bool isStaging
    required property bool isEdited
    required property string hotkey
    required property bool keepResident
    required property bool isPost

    property bool canClose: true
    property bool dragging: false
    property bool renaming: false

    signal menuRequested(Item tab, real x, real y)
    signal dragMoved(real sceneX)
    signal dragEnded()

    readonly property bool current: isEdited && AppController.inScene

    function startRename() {
        renaming = true
        nameField.text = root.name
        nameField.selectAll()
        nameField.forceActiveFocus()
    }

    function finishRename(commit) {
        if (!renaming)
            return
        renaming = false
        const text = nameField.text.trim()
        if (commit && text.length > 0 && text !== root.name)
            AppController.setSceneProps(root.sceneId, { name: text })
    }

    implicitWidth: Math.min(220, row.implicitWidth + 2 * Theme.spacingSm)
    implicitHeight: 30
    opacity: dragging ? 0.5 : 1

    Rectangle {
        anchors.fill: parent
        color: root.current ? Theme.panelBackground : hover.hovered ? Theme.popoverHover : Theme.panelSecondaryBg
        border.color: Theme.panelBorder
        border.width: root.current ? 0 : 1
    }

    Rectangle {
        visible: root.color.length > 0
        anchors.top: parent.top
        width: parent.width
        height: 3
        color: root.color
    }

    HoverHandler { id: hover }

    MouseArea {
        id: area

        property real pressX: 0

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        enabled: !root.renaming
        preventStealing: root.dragging

        onPressed: mouse => pressX = mouse.x
        onPositionChanged: mouse => {
            if (!(mouse.buttons & Qt.LeftButton))
                return
            if (!root.dragging && Math.abs(mouse.x - pressX) > 6)
                root.dragging = true
            if (root.dragging)
                root.dragMoved(mapToItem(null, mouse.x, mouse.y).x)
        }
        onReleased: {
            if (root.dragging) {
                root.dragging = false
                root.dragEnded()
            }
        }
        onClicked: mouse => {
            if (mouse.button === Qt.LeftButton)
                AppController.enterScene(root.sceneId)
            else if (mouse.button === Qt.MiddleButton && root.canClose)
                AppController.deleteScene(root.sceneId)
            else if (mouse.button === Qt.RightButton)
                root.menuRequested(root, mouse.x, mouse.y)
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton)
                root.startRename()
        }
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.leftMargin: Theme.spacingSm
        anchors.rightMargin: Theme.spacingXs
        spacing: Theme.spacingXs

        ThemedLabel {
            visible: !root.renaming
            Layout.fillWidth: true
            Layout.maximumWidth: 140
            tone: root.current ? "default" : "muted"
            elide: Text.ElideRight
            text: root.name
        }
        ThemedTextField {
            id: nameField
            visible: root.renaming
            Layout.preferredWidth: 120
            onAccepted: root.finishRename(true)
            onActiveFocusChanged: if (!activeFocus) root.finishRename(true)
            Keys.onEscapePressed: root.finishRename(false)
        }
        Rectangle {
            visible: root.keepResident
            implicitWidth: 6
            implicitHeight: 6
            radius: 3
            color: Theme.primary
            ToolTip.visible: residentHover.hovered
            ToolTip.text: qsTr("Kept resident")
            HoverHandler { id: residentHover }
        }
        Rectangle {
            visible: root.isPost
            implicitWidth: postText.implicitWidth + Theme.spacingSm
            implicitHeight: postText.implicitHeight + 2
            radius: Theme.radiusSm
            color: Theme.primary
            ToolTip.visible: postHover.hovered
            ToolTip.text: qsTr("Post-processing scene")
            HoverHandler { id: postHover }
            ThemedLabel {
                id: postText
                anchors.centerIn: parent
                color: Theme.primaryForeground
                text: qsTr("POST")
            }
        }
        ThemedLabel {
            visible: root.hotkey.length > 0
            text: root.hotkey
        }
        Rectangle {
            visible: root.isLive || root.isStaging
            implicitWidth: badgeText.implicitWidth + Theme.spacingSm
            implicitHeight: badgeText.implicitHeight + 2
            radius: Theme.radiusSm
            color: root.isLive ? Theme.destructive : Theme.primary
            ThemedLabel {
                id: badgeText
                anchors.centerIn: parent
                color: root.isLive ? "white" : Theme.primaryForeground
                text: root.isLive ? qsTr("LIVE") : qsTr("STAGING")
            }
        }
        IconButton {
            glyph: "x"
            tooltip: qsTr("Close scene")
            buttonSize: 20
            iconSize: 12
            enabled: root.canClose
            opacity: root.canClose ? 1 : 0.3
            onClicked: AppController.deleteScene(root.sceneId)
        }
    }
}

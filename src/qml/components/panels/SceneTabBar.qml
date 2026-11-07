pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

// One tab per scene above the node graph. Tabs only choose what is edited; Go sends the
// edited scene live through Staging and the configured transition.
Rectangle {
    id: root

    readonly property var colors: ["", "#e5484d", "#f76b15", "#ffc53d", "#30a46c", "#0090ff", "#8e4ec6"]
    property int dragFrom: -1
    property int dropIndex: -1

    implicitHeight: 32
    color: Theme.panelSecondaryBg

    ThemedContextMenu {
        id: tabMenu

        property string sceneId: ""
        property Item tab: null
        readonly property bool isLive: tab ? tab.isLive : false

        ThemedMenuItem {
            text: qsTr("Rename")
            onTriggered: tabMenu.tab.startRename()
        }
        ThemedMenuItem {
            text: qsTr("Duplicate")
            onTriggered: AppController.duplicateScene(tabMenu.sceneId)
        }
        ThemedContextMenu {
            title: qsTr("Colour")
            Repeater {
                model: root.colors
                ThemedMenuItem {
                    required property string modelData
                    text: modelData.length > 0 ? modelData : qsTr("None")
                    onTriggered: AppController.setSceneProps(tabMenu.sceneId, { color: modelData })
                }
            }
        }
        ThemedMenuItem {
            text: tabMenu.tab && tabMenu.tab.keepResident ? qsTr("Stop keeping resident") : qsTr("Keep resident")
            onTriggered: AppController.setSceneProps(tabMenu.sceneId, { keepResident: !tabMenu.tab.keepResident })
        }
        ThemedMenuItem {
            text: tabMenu.tab && tabMenu.tab.isPost ? qsTr("Stop using as post-processing scene") : qsTr("Use as post-processing scene")
            onTriggered: AppController.setPostSceneId(tabMenu.tab.isPost ? "" : tabMenu.sceneId)
        }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Stage")
            enabled: !tabMenu.isLive
            onTriggered: AppController.stageScene(tabMenu.sceneId)
        }
        ThemedMenuItem {
            text: qsTr("Go Live")
            enabled: !tabMenu.isLive
            onTriggered: AppController.stageAndTake(tabMenu.sceneId)
        }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Delete")
            icon.name: "trash-2"
            enabled: tabMenu.tab ? tabMenu.tab.canClose : false
            onTriggered: AppController.deleteScene(tabMenu.sceneId)
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.rightMargin: Theme.spacingSm
        spacing: Theme.spacingXs

        ListView {
            id: tabs

            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.maximumWidth: contentWidth
            orientation: ListView.Horizontal
            clip: true
            spacing: 0
            boundsBehavior: Flickable.StopAtBounds
            interactive: root.dragFrom < 0
            model: AppController.scenes

            delegate: SceneTab {
                id: tab

                height: ListView.view.height
                canClose: !isLive && tabs.count > 1

                onMenuRequested: (item, x, y) => {
                    tabMenu.sceneId = sceneId
                    tabMenu.tab = item
                    tabMenu.popup(item, x, y)
                }
                onDragMoved: sceneX => {
                    root.dragFrom = index
                    const p = tabs.contentItem.mapFromItem(null, sceneX, 0)
                    const to = tabs.indexAt(p.x, tabs.height / 2)
                    if (to >= 0)
                        root.dropIndex = to
                }
                onDragEnded: {
                    const to = root.dropIndex
                    const from = root.dragFrom
                    root.dragFrom = -1
                    root.dropIndex = -1
                    if (to >= 0 && to !== from)
                        AppController.moveScene(sceneId, to)
                }

                Rectangle {
                    visible: root.dragFrom >= 0 && root.dropIndex === tab.index && root.dropIndex !== root.dragFrom
                    width: 2
                    height: parent.height
                    color: Theme.primary
                    x: root.dropIndex < root.dragFrom ? 0 : parent.width - 2
                }
            }
        }

        IconButton {
            glyph: "plus"
            tooltip: qsTr("Add scene")
            onClicked: AppController.enterScene(AppController.addScene())
        }

        Item { Layout.fillWidth: true }

        IconButton {
            glyph: "play"
            text: qsTr("Go")
            tooltip: qsTr("Send the edited scene live (Ctrl+Return)")
            enabled: AppController.inScene && AppController.editedSceneId !== AppController.liveSceneId
            onClicked: AppController.stageAndTake(AppController.editedSceneId)
        }
    }
}

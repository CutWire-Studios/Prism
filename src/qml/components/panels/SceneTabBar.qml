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

    CascadeMenu {
        id: tabMenu

        function show(sceneId, tab, x, y) {
            model = [
                { text: qsTr("Rename"), action: () => tab.startRename() },
                { text: qsTr("Duplicate"), action: () => AppController.duplicateScene(sceneId) },
                { text: qsTr("Colour"), children: root.colors.map(c => ({
                      text: c.length > 0 ? c : qsTr("None"),
                      action: () => AppController.setSceneProps(sceneId, { color: c }) })) },
                { text: tab.keepResident ? qsTr("Stop keeping resident") : qsTr("Keep resident"),
                  action: () => AppController.setSceneProps(sceneId, { keepResident: !tab.keepResident }) },
                { text: tab.isPost ? qsTr("Stop using as post-processing scene") : qsTr("Use as post-processing scene"),
                  action: () => AppController.setPostSceneId(tab.isPost ? "" : sceneId) },
                { separator: true },
                { text: qsTr("Stage"), enabled: !tab.isLive, action: () => AppController.stageScene(sceneId) },
                { text: qsTr("Go Live"), enabled: !tab.isLive, action: () => AppController.stageAndTake(sceneId) },
                { separator: true },
                { text: qsTr("Delete"), icon: "trash-2", enabled: tab.canClose,
                  action: () => AppController.deleteScene(sceneId) }
            ]
            popupAt(tab, x, y)
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

                onMenuRequested: (item, x, y) => tabMenu.show(sceneId, item, x, y)
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

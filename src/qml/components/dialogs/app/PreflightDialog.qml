pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Prism

ThemedDialog {
    id: root

    property var issues: []

    title: qsTr("Preflight check")
    showAccept: false
    rejectText: qsTr("Close")
    rejectVariant: "primary"
    preferredWidth: Theme.dialogWidthLg

    onAboutToShow: issues = AppController.preflight()

    contentItem: Column {
        spacing: Theme.spacingMd
        width: parent ? parent.width : Theme.dialogWidthLg

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: root.issues.length === 0
                  ? qsTr("No missing media or devices in any scene.")
                  : qsTr("%n problem(s) found across the scenes.", "", root.issues.length)
        }

        Flickable {
            id: flick
            width: parent.width
            height: Math.min(issueColumn.implicitHeight, root.availableContentHeight - 160)
            contentHeight: issueColumn.implicitHeight
            clip: true
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: issueColumn
                width: flick.width
                spacing: Theme.spacingSm

                Repeater {
                    model: root.issues

                    ThemedLabel {
                        id: issue
                        required property var modelData
                        width: issueColumn.width
                        wrapMode: Text.WordWrap
                        tone: "default"
                        text: (issue.modelData.kind === "missing_file" ? qsTr("Missing file")
                               : issue.modelData.kind === "missing_scene" ? qsTr("Missing scene")
                               : issue.modelData.kind === "scene_cycle" ? qsTr("Scene loops back into itself")
                               : issue.modelData.kind === "history_memory" ? qsTr("Frame history memory")
                               : issue.modelData.kind === "resident_memory" ? qsTr("Too many resident scenes")
                               : issue.modelData.kind === "missing_post_scene" ? qsTr("Missing post-processing scene")
                               : issue.modelData.kind === "post_without_input" ? qsTr("Post-processing scene without Scene input")
                               : qsTr("Missing device"))
                              + " - " + issue.modelData.scene + " / " + issue.modelData.source + "\n" + issue.modelData.detail
                    }
                }
            }
        }
    }
}

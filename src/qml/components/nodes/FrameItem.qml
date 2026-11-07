pragma ComponentBehavior: Bound
import QtQuick
import Prism

// A frame (backdrop) behind the nodes: coloured, titled and optionally annotated. Only the header,
// the comment text and the resize corner take the mouse; the rest of the body is transparent to
// the canvas so the rubber band and the add menu still work inside a frame.
Item {
    id: root

    required property var model
    required property var canvas

    readonly property var controller: canvas.controller
    readonly property var frameId: model.frameId
    readonly property bool selected: controller.selectedFrame == frameId
    readonly property color tint: model.color.length > 0 ? Qt.color(model.color) : Theme.mutedForeground
    readonly property real headerH: 28
    property bool editingTitle: false
    property bool editingComment: false

    function beginTitleEdit() {
        editingComment = false
        titleEdit.text = model.title
        editingTitle = true
        titleEdit.forceActiveFocus()
        titleEdit.selectAll()
    }

    function beginCommentEdit() {
        editingTitle = false
        commentEdit.text = model.comment
        editingComment = true
        commentEdit.forceActiveFocus()
        commentEdit.cursorPosition = commentEdit.length
    }

    function commitTitle() {
        if (!editingTitle)
            return
        editingTitle = false
        if (titleEdit.text !== model.title)
            controller.setFrameProps(frameId, { title: titleEdit.text })
        canvas.forceActiveFocus()
    }

    function commitComment() {
        if (!editingComment)
            return
        editingComment = false
        if (commentEdit.text !== model.comment)
            controller.setFrameProps(frameId, { comment: commentEdit.text })
        canvas.forceActiveFocus()
    }

    x: model.x
    y: model.y
    width: model.width
    height: model.height

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLg
        color: Qt.alpha(root.tint, 0.1)
        border.width: root.selected ? 2 : 1
        border.color: root.selected ? Theme.primary : Qt.alpha(root.tint, 0.55)
    }

    Rectangle {
        id: header
        width: parent.width
        height: root.headerH
        radius: Theme.radiusLg
        color: Qt.alpha(root.tint, 0.25)

        Text {
            id: titleText
            visible: !root.editingTitle
            x: Theme.spacingLg
            width: parent.width - 2 * Theme.spacingLg
            height: parent.height
            text: root.model.title
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.DemiBold
            color: Theme.panelForeground
        }

        TextInput {
            id: titleEdit
            visible: root.editingTitle
            x: titleText.x
            width: titleText.width
            height: parent.height
            verticalAlignment: TextInput.AlignVCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.DemiBold
            color: Theme.panelForeground
            selectByMouse: true
            clip: true
            onAccepted: root.commitTitle()
            onActiveFocusChanged: if (!activeFocus) root.commitTitle()
            Keys.onEscapePressed: {
                root.editingTitle = false
                root.canvas.forceActiveFocus()
            }
        }

        MouseArea {
            id: headerArea
            anchors.fill: parent
            enabled: !root.editingTitle
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            property point startWorld: Qt.point(0, 0)
            property point startPos: Qt.point(0, 0)
            property bool moved: false

            onPressed: (mouse) => {
                root.canvas.forceActiveFocus()
                root.canvas.selectedEdge = 0
                root.controller.selectFrame(root.frameId)
                if (mouse.button === Qt.RightButton) {
                    root.canvas.frameMenuRequested(root.frameId, mapToItem(root.canvas, mouse.x, mouse.y))
                    return
                }
                moved = false
                startWorld = mapToItem(root.canvas.world, mouse.x, mouse.y)
                startPos = Qt.point(root.x, root.y)
            }
            onPositionChanged: (mouse) => {
                if (!pressed || !(pressedButtons & Qt.LeftButton))
                    return
                const w = mapToItem(root.canvas.world, mouse.x, mouse.y)
                const dx = w.x - startWorld.x
                const dy = w.y - startWorld.y
                if (!moved) {
                    if (Math.hypot(dx, dy) * root.canvas.zoom < 4)
                        return
                    moved = true
                    root.canvas.userMoved = true
                }
                root.controller.moveFrame(root.frameId, startPos.x + dx, startPos.y + dy)
            }
            onReleased: root.controller.endMove()
            onDoubleClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton)
                    root.beginTitleEdit()
            }
        }
    }

    Text {
        id: commentText
        visible: !root.editingComment
        x: Theme.spacingLg
        y: root.headerH + Theme.spacingMd
        width: parent.width - 2 * Theme.spacingLg
        height: Math.max(0, parent.height - root.headerH - 2 * Theme.spacingMd)
        text: root.model.comment.length > 0 ? root.model.comment
                                            : (root.selected ? qsTr("Double-click to add a comment") : "")
        wrapMode: Text.Wrap
        elide: Text.ElideRight
        clip: true
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        color: root.model.comment.length > 0 ? Theme.panelForeground : Theme.mutedForeground
    }

    TextEdit {
        id: commentEdit
        visible: root.editingComment
        x: commentText.x
        y: commentText.y
        width: commentText.width
        height: commentText.height
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        clip: true
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        color: Theme.panelForeground
        onActiveFocusChanged: if (!activeFocus) root.commitComment()
        Keys.onEscapePressed: {
            root.editingComment = false
            root.canvas.forceActiveFocus()
        }
        Keys.onPressed: (event) => {
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    && (event.modifiers & Qt.ControlModifier)) {
                root.commitComment()
                event.accepted = true
            }
        }
    }

    MouseArea {
        x: 0
        y: root.headerH
        width: parent.width
        height: Math.max(24, Math.min(commentText.contentHeight + 2 * Theme.spacingMd,
                                      parent.height - root.headerH - 12))
        enabled: !root.editingComment
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: (mouse) => {
            root.canvas.forceActiveFocus()
            root.canvas.selectedEdge = 0
            root.controller.selectFrame(root.frameId)
            if (mouse.button === Qt.RightButton)
                root.canvas.frameMenuRequested(root.frameId, mapToItem(root.canvas, mouse.x, mouse.y))
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton)
                root.beginCommentEdit()
        }
    }

    Rectangle {
        id: grip
        width: 12
        height: 12
        radius: 3
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 2
        color: Qt.alpha(root.selected ? Theme.primary : root.tint, 0.7)
        opacity: root.selected || gripArea.containsMouse ? 1 : 0.4

        MouseArea {
            id: gripArea
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.SizeFDiagCursor
            preventStealing: true

            property point startWorld: Qt.point(0, 0)
            property size startSize: Qt.size(0, 0)

            onPressed: (mouse) => {
                root.canvas.forceActiveFocus()
                root.controller.selectFrame(root.frameId)
                startWorld = mapToItem(root.canvas.world, mouse.x, mouse.y)
                startSize = Qt.size(root.width, root.height)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const w = mapToItem(root.canvas.world, mouse.x, mouse.y)
                root.canvas.userMoved = true
                root.controller.resizeFrame(root.frameId, startSize.width + w.x - startWorld.x,
                                            startSize.height + w.y - startWorld.y)
            }
            onReleased: root.controller.endMove()
        }
    }
}

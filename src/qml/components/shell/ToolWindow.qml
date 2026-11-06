import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Prism

// Root for the clip tools (segmentation, denoise, curves, multicam, restore, downloads).
// A real top-level Window on desktop. On Android a second top-level Window gets no
// safe-area insets and its own GL surface, so the tool is a full-screen modal Popup in the
// main window's overlay instead. Callers and content see the same Window-shaped API either way.
//
// `visible` is the Item's own, so LazyLoader.shown and the Back handlers keep working. Content
// children are reparented out of this (empty) Item into the window or popup; `root.width` and
// `root.height` track that content area, not the Item's declared initial size.
Item {
    id: root

    default property alias content: holder.data

    property string title: ""
    property color color: {
        const opacity = ThemeManager.window.backgroundOpacity
        const multiplier = (opacity === undefined || opacity === null) ? 1 : opacity
        return WindowBackdrop.windowBackground(Theme.c.toolWindow.background, multiplier)
    }
    property real minimumWidth: 0
    property real minimumHeight: 0

    // Same contract as Window.closing: a handler may set close.accepted = false to veto.
    signal closing(var close)

    readonly property bool _android: Qt.platform.os === "android"
    property var _host: null

    visible: false

    function show() { root.visible = true }
    function raise() { if (root._host && !root._android) root._host.raise() }
    function requestActivate() { if (root._host && !root._android) root._host.requestActivate() }

    function close() {
        if (!root._android) {
            if (root._host)
                root._host.close()
            return
        }
        if (!root.visible)
            return
        const event = { accepted: true }
        root.closing(event)
        if (event.accepted)
            root.visible = false
    }

    Item {
        id: holder
        anchors.fill: parent
    }

    Component {
        id: windowComponent

        Window {
            id: toolHost
            title: root.title
            color: root.color
            minimumWidth: root.minimumWidth
            minimumHeight: root.minimumHeight

            onClosing: (close) => root.closing(close)
            onVisibleChanged: if (root.visible !== visible) root.visible = visible
            Component.onCompleted: WindowBackdrop.attach(toolHost)
        }
    }

    Component {
        id: popupComponent

        Popup {
            id: popup
            parent: Overlay.overlay
            x: 0
            y: 0
            width: parent ? parent.width : 0
            height: parent ? parent.height : 0
            padding: 0
            margins: 0
            modal: true
            dim: false
            focus: true
            // Back is routed by the shell (closeTopToolWindow), not by the popup.
            closePolicy: Popup.NoAutoClose

            readonly property var hostWindow: Overlay.overlay ? Overlay.overlay.Window.window : null
            property alias area: area

            onOpened: {
                if (hostWindow && hostWindow.pushOverlayModal)
                    hostWindow.pushOverlayModal()
            }
            onClosed: {
                if (hostWindow && hostWindow.popOverlayModal)
                    hostWindow.popOverlayModal()
                if (root.visible)
                    root.visible = false
            }

            background: Rectangle { color: root.color }

            // The popup fills the overlay, which is what carries the safe-area insets.
            contentItem: Item {
                Item {
                    id: area
                    anchors.fill: parent
                    anchors.leftMargin: parent.SafeArea.margins.left
                    anchors.rightMargin: parent.SafeArea.margins.right
                    anchors.topMargin: parent.SafeArea.margins.top
                    anchors.bottomMargin: Math.max(parent.SafeArea.margins.bottom, Theme.keyboardInset)
                }
            }
        }
    }

    function _syncHost() {
        if (!root._host)
            return
        if (root._android) {
            if (root.visible && !root._host.visible)
                root._host.open()
            else if (!root.visible && root._host.visible)
                root._host.close()
        } else if (root._host.visible !== root.visible) {
            root._host.visible = root.visible
        }
    }

    onVisibleChanged: root._syncHost()

    Component.onCompleted: {
        const initialWidth = root.width
        const initialHeight = root.height
        if (root._android) {
            root._host = popupComponent.createObject(root)
            holder.parent = root._host.area
        } else {
            root._host = windowComponent.createObject(root, { width: initialWidth, height: initialHeight })
            holder.parent = root._host.contentItem
        }
        root.width = Qt.binding(() => holder.width)
        root.height = Qt.binding(() => holder.height)
        root._syncHost()
    }
}

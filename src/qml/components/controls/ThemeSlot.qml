import QtQuick
import Prism

// Trusted theme override for one visual slot. An empty override URL instantiates
// `fallback` synchronously, so a default theme paints the same as an inlined item.
// The loaded item fills this slot; implicit size is forwarded for control layout.
Item {
    id: root

    property string slot: ""
    property var control: null
    property Component fallback: null

    // An override URL that failed to load. themeChanged clears it so a fixed file
    // is tried again, and the error path does not immediately call setSource again.
    property string _brokenUrl: ""
    property bool _ready: false

    // Loader.item is typed as QObject in qmllint; it is an Item at runtime.
    // qmllint disable incompatible-type
    readonly property Item loadedItem: loader.item
    // qmllint enable incompatible-type
    implicitWidth: loadedItem ? loadedItem.implicitWidth : 0
    implicitHeight: loadedItem ? loadedItem.implicitHeight : 0

    function _apply() {
        const url = ThemeManager.overrideUrl(slot)
        const urlText = url.toString()
        if (urlText.length === 0 || urlText === _brokenUrl) {
            // Leave the initial sourceComponent binding alone when it is already
            // showing the fallback, so the default path never reloads.
            if (loader.source.toString().length !== 0)
                loader.sourceComponent = fallback
            return
        }
        loader.setSource(url, { "control": control })
    }

    Component.onCompleted: {
        _ready = true
        _apply()
    }

    onSlotChanged: {
        if (!_ready)
            return
        _brokenUrl = ""
        _apply()
    }

    onControlChanged: {
        if (loader.item && loader.source.toString().length > 0)
            loader.item.control = control
    }

    Connections {
        target: ThemeManager
        function onThemeChanged() {
            if (!root._ready)
                return
            root._brokenUrl = ""
            root._apply()
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        asynchronous: false
        sourceComponent: root.fallback

        onStatusChanged: {
            if (status !== Loader.Error)
                return
            const failedUrl = source.toString()
            console.warn("ThemeSlot: failed to load override", failedUrl, "for slot", root.slot)
            root._brokenUrl = failedUrl
            sourceComponent = root.fallback
        }

        onLoaded: {
            if (root.loadedItem)
                root.loadedItem.anchors.fill = loader
        }
    }
}

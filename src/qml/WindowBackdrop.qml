pragma Singleton
import QtQuick

// Stand-in for the native window backdrop backend, which is not ported. Translucent windows are
// never enabled, so every window keeps its opaque theme colour.
QtObject {
    property bool translucentWindows: false
    readonly property bool restartRequired: false
    readonly property var supportedBackdrops: []
    readonly property string backdrop: ""
    readonly property bool translucencyActive: false

    function windowBackground(color, opacity) {
        return color
    }

    function attach(window) {
    }
}

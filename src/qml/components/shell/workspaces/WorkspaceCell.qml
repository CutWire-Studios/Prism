import QtQuick
import QtQuick.Controls.Basic
import Prism

// One child of a WorkspaceSplit: carries the SplitView sizing for the node inside it.
Item {
    id: root

    property var view: null
    property real fraction: 0
    property bool fill: false
    property bool leaf: true
    property real minPx: 0

    readonly property bool horizontal: root.view?.orientation === Qt.Horizontal
    readonly property real extent: root.view ? (root.horizontal ? root.view.width : root.view.height) : 0

    // Never set a minimum larger than the current extent: on the first layout pass it is 0,
    // and Qt SplitView asserts when maximum < minimum (qBound). Every minimum here is a
    // fraction of the extent or capped by one.
    readonly property real minExtent: root.leaf ? Math.min(root.minPx, root.extent * 0.2)
                                                : root.extent * 0.3
    readonly property real maxExtent: root.fill ? Infinity
                                                : Math.max(root.minExtent, root.extent * 0.85)
    readonly property real preferred: Math.max(0, root.extent * root.fraction)

    SplitView.fillWidth: root.horizontal && root.fill
    SplitView.fillHeight: !root.horizontal && root.fill
    SplitView.preferredWidth: root.horizontal ? root.preferred : -1
    SplitView.preferredHeight: root.horizontal ? -1 : root.preferred
    SplitView.minimumWidth: root.horizontal ? root.minExtent : 0
    SplitView.minimumHeight: root.horizontal ? 0 : root.minExtent
    SplitView.maximumWidth: root.horizontal ? root.maxExtent : Infinity
    SplitView.maximumHeight: root.horizontal ? Infinity : root.maxExtent

    // A handle drag makes SplitView overwrite the attached preferred size, which drops the
    // binding above. Restoring it keeps the cell proportional when the window resizes.
    function rebind() {
        if (root.horizontal)
            SplitView.preferredWidth = Qt.binding(() => root.horizontal ? root.preferred : -1)
        else
            SplitView.preferredHeight = Qt.binding(() => root.horizontal ? -1 : root.preferred)
    }
}

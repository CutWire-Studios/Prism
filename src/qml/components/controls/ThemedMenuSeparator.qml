import QtQuick
import QtQuick.Controls.Basic
import Prism

// Themed divider for ThemedContextMenu.
//
// The Basic style draws a near-white rule, which read as a bright band across
// the dark menu.
MenuSeparator {
    id: root

    padding: Theme.spacingXs
    topPadding: Theme.spacingXs
    bottomPadding: Theme.spacingXs
    // A hidden entry still gets a row in the Menu's ListView; collapse it.
    height: visible ? implicitHeight : 0

    contentItem: Rectangle {
        implicitHeight: Theme.c.themedMenuSeparator.borderWidth
        color: Theme.c.themedMenuSeparator.background
    }
}

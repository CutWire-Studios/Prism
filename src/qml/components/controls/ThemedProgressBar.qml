pragma ComponentBehavior: Bound
import QtQuick
import Prism

// Slim determinate progress bar for long-running jobs shown inline in a panel.
//
// The large ring (LabelledProgressRing) is for dialogs that have the room; this is what a panel
// section uses when a job needs a bar next to a status line and a Cancel button.
Item {
    id: root

    // 0..1, clamped.
    property real value: 0
    property real barHeight: Theme.spacingMd

    implicitHeight: barHeight
    height: barHeight

    ThemeSlot {
        anchors.fill: parent
        slot: "ThemedProgressBarBackground"
        control: root
        fallback: Component {
            Rectangle {
                radius: Theme.c.themedProgressBar.trackRadius
                color: Theme.c.themedProgressBar.trackBackground

                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, root.value))
                    height: parent.height
                    radius: parent.radius
                    color: Theme.c.themedProgressBar.fillBackground

                    Behavior on width {
                        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                    }
                }
            }
        }
    }
}

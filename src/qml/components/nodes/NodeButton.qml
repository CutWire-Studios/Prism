import QtQuick
import Prism

// Compact button for node bodies; nodes are a fixed few dozen pixels tall, so the standard
// control height does not fit.
ThemedButton {
    height: 20
    focusPolicy: Qt.NoFocus
    topPadding: 0
    bottomPadding: 0
    leftPadding: Theme.spacingMd
    rightPadding: Theme.spacingMd
    radius: Theme.radiusSm
    font.pixelSize: Theme.fontSizeXs
}

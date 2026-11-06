pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Prism

ThemeSlot {
    id: handleRect

    required property Slider slider

    slot: "ThemedSliderHandle"
    control: slider

    x: handleRect.slider.leftPadding + handleRect.slider.visualPosition * (handleRect.slider.availableWidth - width)
    y: handleRect.slider.topPadding + handleRect.slider.availableHeight / 2 - height / 2
    width: Theme.touchInput ? 20 : Theme.iconSizeMd
    height: width
    // Grows slightly on hover so the grab target is legible.
    scale: handleRect.slider.pressed ? 1.1 : (handleRect.slider.hovered || handleRect.slider.visualFocus ? 1.05 : 1.0)

    Behavior on x {
        enabled: !handleRect.slider.pressed
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingInOut }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    fallback: Component {
        Rectangle {
            radius: Theme.c.themedSliderHandle.radius
            color: handleRect.slider.pressed ? Theme.c.themedSliderHandle.backgroundPressed
                    : (handleRect.slider.hovered ? Qt.lighter(Theme.c.themedSliderHandle.background, 1.25)
                                                 : Qt.lighter(Theme.c.themedSliderHandle.background, 1.15))
            border.width: Theme.c.themedSliderHandle.borderWidth
            border.color: handleRect.slider.visualFocus ? Theme.c.themedSliderHandle.borderFocus
                                                        : Theme.c.themedSliderHandle.border

            Behavior on color {
                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
        }
    }
}

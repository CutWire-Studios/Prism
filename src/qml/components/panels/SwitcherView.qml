import QtQuick
import QtQuick.Layouts
import Prism

// Live/Staging switcher: transition progress, cut/take, transition mode and duration, and the panic buttons.
ColumnLayout {
    id: root

    readonly property bool locked: AppController.singleStream

    spacing: Theme.spacingSm

    // Slide-to-go-live: dragging scrubs the transition, reaching the end commits the take,
    // releasing early springs back. The thumb always returns to the left after a take.
    Rectangle {
        id: slide

        property real pos: 0
        property bool committed: false
        property bool pushOnSnap: false
        readonly property real maxX: width - thumb.width - 2 * thumb.inset

        Layout.fillWidth: true
        Layout.preferredHeight: Theme.controlHeight + 2 * thumb.inset
        radius: thumb.radius + thumb.inset
        color: Theme.sliderTrack
        border.color: Theme.panelBorder
        opacity: root.locked ? 0.5 : 1

        Accessible.role: Accessible.Slider
        Accessible.name: qsTr("Slide to go live")

        Binding {
            target: slide
            property: "pos"
            value: AppController.faderValue / 100
            when: !dragArea.pressed && !snapBack.running
            restoreMode: Binding.RestoreNone
        }

        onPosChanged: if (snapBack.running && pushOnSnap) AppController.faderValue = Math.round(pos * 100)

        NumberAnimation {
            id: snapBack
            target: slide
            property: "pos"
            to: 0
            duration: 260
            easing.type: Easing.OutCubic
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: thumb.inset
            width: thumb.x + thumb.width - thumb.inset
            radius: thumb.radius
            color: Theme.destructive
            opacity: 0.18 * slide.pos
        }

        ThemedLabel {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: thumb.width / 2
            text: qsTr("slide to go live")
            color: Theme.mutedForeground
            font.pixelSize: Theme.fontSizeBase
            opacity: Math.max(0, 1 - slide.pos * 2.5) * breathe.level

            QtObject {
                id: breathe
                property real level: 1
            }
            SequentialAnimation {
                running: !root.locked && slide.pos === 0 && slide.visible
                loops: Animation.Infinite
                onStopped: breathe.level = 1
                NumberAnimation { target: breathe; property: "level"; to: 0.45; duration: 1100; easing.type: Easing.InOutSine }
                NumberAnimation { target: breathe; property: "level"; to: 1; duration: 1100; easing.type: Easing.InOutSine }
            }
        }

        Rectangle {
            id: thumb

            readonly property real inset: 3

            x: inset + slide.pos * slide.maxX
            y: inset
            width: Math.max(height * 1.3, 52)
            height: slide.height - 2 * inset
            radius: Theme.radiusMd
            color: dragArea.pressed ? Qt.darker(Theme.destructive, 1.12) : Theme.destructive

            IconGlyph {
                anchors.centerIn: parent
                glyph: "chevrons-right"
                iconSize: parent.height * 0.55
                iconColor: "white"
            }
        }

        MouseArea {
            id: dragArea

            property real grabOffset: 0

            anchors.fill: parent
            enabled: !root.locked && !AppController.transitionRunning
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

            onPressed: mouse => {
                snapBack.stop();
                slide.committed = false;
                grabOffset = mouse.x - thumb.x;
                if (grabOffset < 0 || grabOffset > thumb.width)
                    grabOffset = thumb.width / 2;
            }
            onPositionChanged: mouse => {
                if (slide.committed)
                    return;
                const p = Math.max(0, Math.min(1, (mouse.x - grabOffset - thumb.inset) / slide.maxX));
                slide.pos = p;
                AppController.faderValue = Math.round(p * 100);
                if (p >= 1)
                    slide.committed = true;
            }
            onReleased: {
                slide.pushOnSnap = !slide.committed;
                slide.committed = false;
                if (slide.pos > 0)
                    snapBack.start();
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        ThemedButton {
            text: qsTr("Cut")
            tooltip: qsTr("Instant cut from Staging to Live (Space)")
            enabled: !root.locked
            onClicked: AppController.cut()
        }
        ThemedButton {
            text: qsTr("Take")
            variant: "primary"
            tooltip: qsTr("Transition Staging to Live using the selected mode (Enter)")
            enabled: !root.locked && !AppController.transitionRunning
            onClicked: AppController.take()
        }
        ThemedComboBox {
            Layout.fillWidth: true
            enabled: !root.locked
            model: AppController.transitionModeNames
            currentIndex: AppController.transitionMode
            onActivated: index => AppController.transitionMode = index
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        ThemedLabel { text: qsTr("Time") }
        ThemedSlider {
            id: durSlider
            Layout.fillWidth: true
            enabled: !root.locked
            label: qsTr("Transition duration")
            from: 0.05
            to: 5
            valueFormatter: v => Number(v).toFixed(2) + " s"
            onMoved: AppController.transitionDuration = value
            Binding {
                target: durSlider
                property: "value"
                value: AppController.transitionDuration
                when: !durSlider.pressed
            }
        }
        ThemedLabel {
            Layout.preferredWidth: 44
            font.family: Theme.monoFontFamily
            horizontalAlignment: Text.AlignRight
            text: AppController.transitionDuration.toFixed(2) + " s"
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        ThemedToggleButton {
            text: qsTr("Blackout")
            tooltip: qsTr("Cut program output to black (B)")
            checkable: false
            checked: AppController.panicMode === "blackout"
            onClicked: AppController.togglePanic("blackout")
        }
        ThemedToggleButton {
            text: qsTr("Freeze")
            tooltip: qsTr("Freeze the program output on the current frame (P)")
            checkable: false
            checked: AppController.panicMode === "freeze"
            onClicked: AppController.togglePanic("freeze")
        }
        ThemedToggleButton {
            text: qsTr("Stay Tuned")
            tooltip: qsTr("Show a \"Stay Tuned\" holding screen on program output (F9)")
            checkable: false
            checked: AppController.panicMode === "stay_tuned"
            onClicked: AppController.togglePanic("stay_tuned")
        }
    }

    Item { Layout.fillHeight: true }
}

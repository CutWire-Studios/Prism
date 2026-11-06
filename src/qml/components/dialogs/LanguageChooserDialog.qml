import QtQuick
import QtQuick.Controls.Basic
import Prism

// Language picker reopened from the header. Choosing a language applies it at once, so the UI
// retranslates before the dialog closes.
ThemedDialog {
    id: root

    title: qsTr("Language")
    rejectText: qsTr("Close")
    showAccept: false
    preferredWidth: Theme.dialogWidthSm
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string selectedId: ""

    function openFromHeader() {
        selectedId = PreferencesController.uiLanguage
        open()
    }

    onOpened: {
        const langs = PreferencesController.availableLanguages
        for (let i = 0; i < langs.length; ++i) {
            if (langs[i].id === selectedId) {
                languageList.positionViewAtIndex(i, ListView.Contain)
                return
            }
        }
    }

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : 320

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("Language for menus and labels. Takes effect immediately.")
        }

        Rectangle {
            width: parent.width
            height: Math.min(languageList.contentHeight + 2,
                             Math.min(280, root.availableContentHeight - 48))
            radius: Theme.c.languageChooserDialog.radius
            color: Theme.c.languageChooserDialog.background
            border.width: Theme.c.languageChooserDialog.borderWidth
            border.color: Theme.c.languageChooserDialog.border
            clip: true

            ListView {
                id: languageList
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: PreferencesController.availableLanguages
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    width: languageList.width
                    height: 44
                    highlighted: modelData.id === root.selectedId
                    hoverEnabled: true

                    background: Rectangle {
                        color: {
                            if (row.highlighted)
                                return Theme.c.languageChooserDialog.backgroundSelected
                            if (row.hovered)
                                return Theme.c.languageChooserDialog.backgroundHover
                            return "transparent"
                        }
                    }

                    contentItem: Item {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12

                        Text {
                            anchors.left: parent.left
                            anchors.right: checkIcon.left
                            anchors.rightMargin: Theme.spacingLg
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.label
                            elide: Text.ElideRight
                            color: row.highlighted
                                   ? Theme.c.languageChooserDialog.foregroundSelected
                                   : Theme.c.languageChooserDialog.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: row.highlighted ? Font.Medium : Font.Normal
                        }

                        IconGlyph {
                            id: checkIcon
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icons.check
                            iconSize: 16
                            iconColor: Theme.c.languageChooserDialog.icon
                            visible: row.highlighted
                        }
                    }

                    onClicked: {
                        root.selectedId = modelData.id
                        PreferencesController.uiLanguage = modelData.id
                        root.close()
                    }
                }
            }
        }
    }
}

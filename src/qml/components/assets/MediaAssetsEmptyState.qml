import QtQuick
import Prism
// First-run screen for an empty library: how media gets in, and what is accepted. Hand-built
// rather than the shared EmptyState because it carries two actions and the format table, and
// centres in the whole panel.
Item {
    id: root

    property bool shown: false
    signal importRequested()
    signal importFolderRequested()

    anchors.fill: parent
    visible: shown
    clip: true

    readonly property real sectionGap: Theme.spacingXl
    readonly property real buttonsHeight: importRow.height + sectionGap
    readonly property real textHeight: titleText.implicitHeight + hintText.implicitHeight + sectionGap
    readonly property real tableHeight: formatBox.height + sectionGap
    readonly property bool showButtons: height >= 48 + sectionGap + textHeight + buttonsHeight
    readonly property bool showFormats: showButtons
        && height >= 48 + sectionGap + textHeight + buttonsHeight + tableHeight
    readonly property bool showIcon: height >= 48 + sectionGap + textHeight
    readonly property bool showHint: height >= titleText.implicitHeight + hintText.implicitHeight + sectionGap

    Column {
        anchors.centerIn: parent
        width: Math.max(1, Math.min(parent.width - Theme.spacing3xl * 2, 300))
        spacing: Theme.spacingXl

        Rectangle {
            visible: root.showIcon
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48
            height: 48
            radius: Theme.c.mediaAssetsEmptyState.iconRadius
            color: Theme.c.mediaAssetsEmptyState.iconBackground

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.film
                iconSize: Theme.iconSizeXl
                iconColor: Theme.c.mediaAssetsEmptyState.icon
            }
        }

        Text {
            id: titleText
            width: parent.width
            text: qsTr("No media yet")
            color: Theme.c.mediaAssetsEmptyState.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            id: hintText
            visible: root.showHint
            width: parent.width
            text: qsTr("Import files or drop them here, then double-click or drag them onto the node graph.")
            color: Theme.c.mediaAssetsEmptyState.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Row {
            id: importRow
            visible: root.showButtons
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.spacingLg

            ThemedButton {
                text: qsTr("Import media")
                variant: "primary"
                glyph: Theme.icons.upload
                onClicked: root.importRequested()
            }

            ThemedButton {
                text: qsTr("Import folder")
                glyph: Theme.icons.folderInput
                onClicked: root.importFolderRequested()
            }
        }

        Rectangle {
            id: formatBox
            visible: root.showFormats
            width: parent.width
            height: formatColumn.height + Theme.spacingXl * 2
            radius: Theme.c.mediaAssetsEmptyState.formatsRadius
            color: "transparent"
            border.width: Theme.c.mediaAssetsEmptyState.formatsBorderWidth
            border.color: Theme.c.mediaAssetsEmptyState.formatsBorder

            Column {
                id: formatColumn
                anchors.centerIn: parent
                width: parent.width - Theme.spacingXl * 2
                spacing: Theme.spacingMd

                Text {
                    width: parent.width
                    text: qsTr("Supported formats")
                    color: Theme.c.mediaAssetsEmptyState.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                    font.weight: Font.Medium
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.5
                }

                Repeater {
                    model: [
                        { glyph: Theme.icons.video, label: qsTr("Video"),
                          formats: "MP4 · MOV · MKV · WEBM · AVI" },
                        { glyph: Theme.icons.music, label: qsTr("Audio"),
                          formats: "MP3 · WAV · FLAC · AAC · OGG" },
                        { glyph: Theme.icons.image, label: qsTr("Images"),
                          formats: "PNG · JPG · WEBP · HEIC · GIF" }
                    ]

                    delegate: MediaAssetsFormatRow {
                        columnWidth: formatColumn.width
                    }
                }
            }
        }
    }
}

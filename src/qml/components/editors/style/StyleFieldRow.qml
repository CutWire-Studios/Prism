import QtQuick
import QtQuick.Layouts
import Prism

// A label on the left and the field, or fields, filling the rest: the widget editors' labeledRow.
RowLayout {
    id: root

    property string label: ""
    default property alias content: holder.data

    spacing: Theme.spacingSm

    ThemedLabel {
        Layout.preferredWidth: 78
        Layout.maximumWidth: 78
        text: root.label
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
    }

    RowLayout {
        id: holder
        Layout.fillWidth: true
        spacing: Theme.spacingSm
    }
}

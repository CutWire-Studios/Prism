import QtQuick
import Prism

// Every panel wrapper lives here for the whole session. A layout rebuild only moves
// them between slots and this hidden holder. Every panel type is a PlaceholderPanel until the
// real panels are ported.
Item {
    id: root
    visible: false

    property var _created: ({})

    Component { id: placeholderC; PlaceholderPanel {} }

    function inLayout(type) {
        return !!root._created[type] && root._created[type].parent !== root
    }

    function label(type) {
        const types = Workspaces.panelTypes
        for (let i = 0; i < types.length; ++i) {
            if (types[i].type === type)
                return types[i].fullLabel
        }
        return type
    }

    function panel(type) {
        if (root._created[type])
            return root._created[type]
        const w = placeholderC.createObject(root, { panelType: type, label: root.label(type) })
        root._created = Object.assign({}, root._created, { [type]: w })
        return w
    }

    function parkAll() {
        for (const type in root._created) {
            const item = root._created[type]
            item.anchors.fill = null
            item.parent = root
        }
    }
}

import QtQuick
import Prism

// Every panel wrapper lives here for the whole session. A layout rebuild only moves
// them between slots and this hidden holder.
Item {
    id: root
    visible: false

    property var _created: ({})
    property var editors: null

    Component { id: placeholderC; PlaceholderPanel {} }
    Component { id: decksC; DecksPanel {} }
    Component { id: programC; ProgramPanel {} }
    Component { id: nodeGraphC; NodeGraphPanel { editors: root.editors } }
    Component { id: assetsC; AssetsPanel {} }
    Component { id: inspectorC; InspectorPanel { editors: root.editors } }
    Component { id: mixerC; MixerPanel {} }
    Component { id: outputsC; OutputsPanel {} }

    function componentFor(type) {
        switch (type) {
        case "decks": return decksC
        case "program": return programC
        case "nodeGraph": return nodeGraphC
        case "assets": return assetsC
        case "inspector": return inspectorC
        case "mixer": return mixerC
        case "outputs": return outputsC
        }
        return null
    }

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
        const c = root.componentFor(type)
        const w = c ? c.createObject(root) : placeholderC.createObject(root, { panelType: type, label: root.label(type) })
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

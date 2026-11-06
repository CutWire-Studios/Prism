.pragma library

// Pure operations on a workspace window's NODE tree. Every function that changes a tree
// works on a copy and returns the new root, so callers can assign it and let bindings refresh.

var MIN_SIZE = 0.08

function clone(value) {
    return value === null || value === undefined ? value : JSON.parse(JSON.stringify(value))
}

function isSplit(node) {
    return !!node && node.split !== undefined
}

function get(root, path) {
    var node = root
    for (var i = 0; i < path.length; ++i) {
        if (!isSplit(node))
            return null
        node = node.children[path[i]]
    }
    return node
}

function typesOf(node, out) {
    out = out || []
    if (!node)
        return out
    if (isSplit(node)) {
        for (var i = 0; i < node.children.length; ++i)
            typesOf(node.children[i], out)
    } else {
        for (var j = 0; j < node.panels.length; ++j)
            out.push(node.panels[j].type)
    }
    return out
}

function placedTypes(ws) {
    var placed = {}
    var windows = (ws && ws.windows) || []
    for (var i = 0; i < windows.length; ++i) {
        var types = typesOf(windows[i].root)
        for (var j = 0; j < types.length; ++j)
            placed[types[j]] = true
    }
    return placed
}

function findSlotPath(root, type, prefix) {
    prefix = prefix || []
    if (!root)
        return null
    if (isSplit(root)) {
        for (var i = 0; i < root.children.length; ++i) {
            var found = findSlotPath(root.children[i], type, prefix.concat([i]))
            if (found)
                return found
        }
        return null
    }
    for (var j = 0; j < root.panels.length; ++j) {
        if (root.panels[j].type === type)
            return prefix
    }
    return null
}

function normalizeSizes(sizes, count) {
    var sum = 0
    for (var i = 0; i < count; ++i)
        sum += sizes[i] > 0 ? sizes[i] : 0
    var out = []
    for (var j = 0; j < count; ++j)
        out.push(sum > 0 ? Math.max(sizes[j], 0) / sum : 1 / count)
    return out
}

// Drops the given panel types, removes slots left empty, collapses splits with one child and
// merges a child split into its parent when both run the same way.
function prune(node, drop) {
    if (!node)
        return null
    if (isSplit(node)) {
        var kids = []
        var sizes = []
        var count = node.children.length
        for (var i = 0; i < count; ++i) {
            var child = prune(node.children[i], drop)
            if (!child)
                continue
            var size = node.sizes && node.sizes.length === count ? node.sizes[i] : 1 / count
            if (isSplit(child) && child.split === node.split) {
                for (var k = 0; k < child.children.length; ++k) {
                    kids.push(child.children[k])
                    sizes.push(child.sizes[k] * size)
                }
            } else {
                kids.push(child)
                sizes.push(size)
            }
        }
        if (kids.length === 0)
            return null
        if (kids.length === 1)
            return kids[0]
        return { split: node.split, children: kids, sizes: normalizeSizes(sizes, kids.length) }
    }
    var current = node.panels[node.active]
    var panels = node.panels.filter(function (p) { return !drop[p.type] })
    if (panels.length === 0)
        return null
    var active = 0
    for (var j = 0; j < panels.length; ++j) {
        if (current && panels[j].type === current.type)
            active = j
    }
    return { panels: panels, active: active }
}

function removePanels(root, types) {
    var drop = {}
    for (var i = 0; i < types.length; ++i)
        drop[types[i]] = true
    return prune(clone(root), drop)
}

// Moves `panels` (objects with type and optional tab) next to the slot holding `targetType`.
// zone is "center" (join as tabs) or an edge, which splits the target. An unknown target
// leaves the tree as it was.
function place(root, panels, targetType, zone) {
    var moving = clone(panels)
    if (!root)
        return { panels: moving, active: 0 }
    var tree = removePanels(root, moving.map(function (p) { return p.type }))
    var path = tree ? findSlotPath(tree, targetType) : null
    if (!path)
        return clone(root)

    var slot = { panels: moving, active: 0 }
    var target = get(tree, path)
    if (zone === "center") {
        target.active = target.panels.length
        target.panels = target.panels.concat(moving)
        return tree
    }

    var dir = zone === "left" || zone === "right" ? "h" : "v"
    var before = zone === "left" || zone === "top"
    var pair = before ? [slot, target] : [target, slot]
    if (path.length === 0)
        return { split: dir, children: pair, sizes: [0.5, 0.5] }

    var parent = get(tree, path.slice(0, -1))
    var index = path[path.length - 1]
    if (parent.split === dir) {
        var half = parent.sizes[index] / 2
        var at = before ? index : index + 1
        parent.sizes[index] = half
        parent.children.splice(at, 0, slot)
        parent.sizes.splice(at, 0, half)
        return tree
    }
    parent.children[index] = { split: dir, children: pair, sizes: [0.5, 0.5] }
    return tree
}

function withSizes(root, path, sizes) {
    var tree = clone(root)
    var node = get(tree, path)
    if (isSplit(node))
        node.sizes = normalizeSizes(sizes, node.children.length)
    return tree
}

function panelAt(node, type) {
    for (var i = 0; i < node.panels.length; ++i) {
        if (node.panels[i].type === type)
            return i
    }
    return -1
}

function withTab(root, type, tab) {
    var tree = clone(root)
    var path = findSlotPath(tree, type)
    if (!path)
        return tree
    var slot = get(tree, path)
    var panel = slot.panels[panelAt(slot, type)]
    if (tab)
        panel.tab = tab
    else
        delete panel.tab
    return tree
}

function withActive(root, type) {
    var tree = clone(root)
    var path = findSlotPath(tree, type)
    if (path) {
        var slot = get(tree, path)
        slot.active = panelAt(slot, type)
    }
    return tree
}

#include "app/editor/WorkspaceStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace {

constexpr auto kListKey = "ui/workspaces/list";
constexpr auto kActiveKey = "ui/workspaces/active";
constexpr auto kDefaultId = "live";
constexpr auto kPresetPath = ":/qt/qml/Prism/resources/workspaces/live.json";

struct PanelType
{
    const char *type;
    const char *label;
};

const PanelType kPanelTypes[] = {
    {"decks", QT_TRANSLATE_NOOP("WorkspaceStore", "Decks")},
    {"program", QT_TRANSLATE_NOOP("WorkspaceStore", "Program")},
    {"nodeGraph", QT_TRANSLATE_NOOP("WorkspaceStore", "Node Graph")},
    {"assets", QT_TRANSLATE_NOOP("WorkspaceStore", "Assets")},
    {"inspector", QT_TRANSLATE_NOOP("WorkspaceStore", "Inspector")},
};

bool isKnownType(const QString &type)
{
    const auto known = [&](const PanelType &p) { return type == QLatin1String(p.type); };
    return std::any_of(std::begin(kPanelTypes), std::end(kPanelTypes), known);
}

bool isSplit(const QVariantMap &node)
{
    return node.contains(QStringLiteral("split"));
}

QVariantList equalSizes(int count)
{
    QVariantList sizes;
    for (int i = 0; i < count; ++i)
        sizes << 1.0 / count;
    return sizes;
}

QVariantList normalizedSizes(const QVariantList &sizes, int count)
{
    if (sizes.size() != count)
        return equalSizes(count);
    double sum = 0;
    for (const QVariant &v : sizes) {
        const double s = v.toDouble();
        if (!std::isfinite(s) || s <= 0)
            return equalSizes(count);
        sum += s;
    }
    QVariantList out;
    for (const QVariant &v : sizes)
        out << v.toDouble() / sum;
    return out;
}

// An empty map means the node holds nothing and should be dropped by its parent.
QVariantMap normalizedNode(const QVariantMap &node)
{
    if (isSplit(node)) {
        QVariantList children;
        const QVariantList raw = node.value(QStringLiteral("children")).toList();
        QVariantList rawSizes = node.value(QStringLiteral("sizes")).toList();
        QVariantList keptSizes;
        for (int i = 0; i < raw.size(); ++i) {
            const QVariantMap child = normalizedNode(raw.at(i).toMap());
            if (child.isEmpty())
                continue;
            children << child;
            // A dropped child takes its size with it; if the sizes did not line up to
            // begin with they are replaced below anyway.
            if (rawSizes.size() == raw.size())
                keptSizes << rawSizes.at(i);
        }
        if (children.isEmpty())
            return {};
        if (children.size() == 1)
            return children.first().toMap();
        QVariantMap out = node;
        out[QStringLiteral("children")] = children;
        out[QStringLiteral("sizes")] = normalizedSizes(keptSizes, children.size());
        const QString dir = node.value(QStringLiteral("split")).toString();
        out[QStringLiteral("split")] = dir == QLatin1String("v") ? dir : QStringLiteral("h");
        return out;
    }

    QVariantList panels;
    for (const QVariant &p : node.value(QStringLiteral("panels")).toList()) {
        if (!p.toMap().value(QStringLiteral("type")).toString().isEmpty())
            panels << p;
    }
    if (panels.isEmpty())
        return {};
    QVariantMap out = node;
    out[QStringLiteral("panels")] = panels;
    out[QStringLiteral("active")] = std::clamp(node.value(QStringLiteral("active")).toInt(), 0,
                                               int(panels.size()) - 1);
    return out;
}

QVariantMap normalizedWorkspace(const QVariantMap &ws)
{
    QVariantMap out = ws;
    QVariantList windows;
    for (const QVariant &w : ws.value(QStringLiteral("windows")).toList()) {
        QVariantMap window = w.toMap();
        window[QStringLiteral("root")] = normalizedNode(window.value(QStringLiteral("root")).toMap());
        windows << window;
    }
    out[QStringLiteral("windows")] = windows;
    return out;
}

bool findWindow(const QVariantMap &ws, const QString &windowId, QVariantList &windows, int &index)
{
    windows = ws.value(QStringLiteral("windows")).toList();
    for (index = 0; index < windows.size(); ++index) {
        if (windows.at(index).toMap().value(QStringLiteral("id")).toString() == windowId)
            return true;
    }
    return false;
}

bool setNodeSizes(QVariantMap &node, const QVariantList &path, int depth, const QVariantList &sizes)
{
    if (!isSplit(node))
        return false;
    QVariantList children = node.value(QStringLiteral("children")).toList();
    if (depth == path.size()) {
        if (sizes.size() != children.size())
            return false;
        node[QStringLiteral("sizes")] = normalizedSizes(sizes, children.size());
        return true;
    }
    const int index = path.at(depth).toInt();
    if (index < 0 || index >= children.size())
        return false;
    QVariantMap child = children.at(index).toMap();
    if (!setNodeSizes(child, path, depth + 1, sizes))
        return false;
    children[index] = child;
    node[QStringLiteral("children")] = children;
    return true;
}

QString validateNode(const QVariantMap &node, QSet<QString> &seen)
{
    if (node.isEmpty())
        return WorkspaceStore::tr("A window has no panels.");

    if (isSplit(node)) {
        const QVariantList children = node.value(QStringLiteral("children")).toList();
        if (children.size() < 2)
            return WorkspaceStore::tr("A split needs at least two sections.");
        for (const QVariant &child : children) {
            const QString err = validateNode(child.toMap(), seen);
            if (!err.isEmpty())
                return err;
        }
        return {};
    }

    const QVariantList panels = node.value(QStringLiteral("panels")).toList();
    if (panels.isEmpty())
        return WorkspaceStore::tr("A section needs at least one panel.");
    const int active = node.value(QStringLiteral("active")).toInt();
    if (active < 0 || active >= panels.size())
        return WorkspaceStore::tr("A section's active tab is out of range.");
    for (const QVariant &p : panels) {
        const QString type = p.toMap().value(QStringLiteral("type")).toString();
        if (!isKnownType(type))
            return WorkspaceStore::tr("Unknown panel type \"%1\".").arg(type);
        if (seen.contains(type))
            return WorkspaceStore::tr("The \"%1\" panel is used more than once.").arg(type);
        seen.insert(type);
    }
    return {};
}

bool containsPanel(const QVariantMap &node, const QString &type)
{
    for (const QVariant &child : node.value(QStringLiteral("children")).toList()) {
        if (containsPanel(child.toMap(), type))
            return true;
    }
    for (const QVariant &panel : node.value(QStringLiteral("panels")).toList()) {
        if (panel.toMap().value(QStringLiteral("type")).toString() == type)
            return true;
    }
    return false;
}

bool workspaceHasPanel(const QVariantMap &ws, const QString &type)
{
    for (const QVariant &window : ws.value(QStringLiteral("windows")).toList()) {
        if (containsPanel(window.toMap().value(QStringLiteral("root")).toMap(), type))
            return true;
    }
    return false;
}

QVariantMap parseJson(const QByteArray &data)
{
    return QJsonDocument::fromJson(data).object().toVariantMap();
}

} // namespace

WorkspaceStore::WorkspaceStore(QObject *parent)
    : QObject(parent)
{
    loadPresets();
    loadUser();
    m_activeId = QSettings().value(QLatin1String(kActiveKey), QLatin1String(kDefaultId)).toString();
    fixActive();
}

void WorkspaceStore::loadPresets()
{
    QFile file{QLatin1String(kPresetPath)};
    if (!file.open(QIODevice::ReadOnly))
        return;
    QVariantMap ws = normalizedWorkspace(parseJson(file.readAll()));
    if (ws.value(QStringLiteral("id")).toString().isEmpty() || !validate(ws).isEmpty())
        return;
    ws[QStringLiteral("builtin")] = true;
    m_presets.insert(ws.value(QStringLiteral("id")).toString(), ws);
    m_presetOrder << ws.value(QStringLiteral("id")).toString();
}

void WorkspaceStore::loadUser()
{
    m_user.clear();
    const QByteArray raw = QSettings().value(QLatin1String(kListKey)).toString().toUtf8();
    const QJsonArray array = QJsonDocument::fromJson(raw).array();
    QSet<QString> ids;
    for (const QJsonValue &v : array) {
        QVariantMap ws = normalizedWorkspace(v.toObject().toVariantMap());
        const QString id = ws.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || ids.contains(id) || !validate(ws).isEmpty())
            continue;
        ids.insert(id);
        ws[QStringLiteral("builtin")] = m_presets.contains(id);
        m_user << ws;
    }
}

void WorkspaceStore::persist() const
{
    QJsonArray array;
    for (const QVariant &v : m_user)
        array.append(QJsonObject::fromVariantMap(v.toMap()));
    QSettings().setValue(QLatin1String(kListKey),
                         QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void WorkspaceStore::fixActive()
{
    if (!exists(m_activeId))
        m_activeId = QLatin1String(kDefaultId);
}

bool WorkspaceStore::exists(const QString &id) const
{
    return m_presets.contains(id) || userIndex(id) >= 0;
}

int WorkspaceStore::userIndex(const QString &id) const
{
    for (int i = 0; i < m_user.size(); ++i) {
        if (m_user.at(i).toMap().value(QStringLiteral("id")).toString() == id)
            return i;
    }
    return -1;
}

QVariantMap WorkspaceStore::effective(const QString &id) const
{
    const int i = userIndex(id);
    if (i >= 0)
        return m_user.at(i).toMap();
    return m_presets.value(id);
}

QString WorkspaceStore::newId() const
{
    for (int n = 1;; ++n) {
        const QString id = QStringLiteral("custom-%1").arg(n);
        if (!exists(id))
            return id;
    }
}

void WorkspaceStore::store(const QVariantMap &ws)
{
    const QString id = ws.value(QStringLiteral("id")).toString();
    const int i = userIndex(id);
    if (i >= 0)
        m_user[i] = ws;
    else
        m_user << ws;
    persist();
}

QVariantList WorkspaceStore::workspaces() const
{
    QVariantList out;
    for (const QString &id : m_presetOrder)
        out << effective(id);
    for (const QVariant &v : m_user) {
        if (!m_presets.contains(v.toMap().value(QStringLiteral("id")).toString()))
            out << v;
    }
    return out;
}

void WorkspaceStore::setActiveId(const QString &id)
{
    if (id == m_activeId || !exists(id))
        return;
    m_activeId = id;
    QSettings().setValue(QLatin1String(kActiveKey), id);
    emit activeIdChanged();
}

QVariantList WorkspaceStore::panelTypes() const
{
    QVariantList out;
    for (const PanelType &p : kPanelTypes) {
        const QString label = tr(p.label);
        out << QVariantMap{{QStringLiteral("type"), QLatin1String(p.type)},
                           {QStringLiteral("label"), label},
                           {QStringLiteral("group"), QStringLiteral("panel")},
                           {QStringLiteral("fullLabel"), label}};
    }
    return out;
}

QVariantMap WorkspaceStore::workspace(const QString &id) const
{
    return effective(id);
}

QString WorkspaceStore::workspaceWithPanel(const QString &type) const
{
    if (workspaceHasPanel(effective(m_activeId), type))
        return m_activeId;
    for (const QString &id : m_presetOrder) {
        if (workspaceHasPanel(effective(id), type))
            return id;
    }
    for (const QVariant &v : m_user) {
        if (workspaceHasPanel(v.toMap(), type))
            return v.toMap().value(QStringLiteral("id")).toString();
    }
    return {};
}

QString WorkspaceStore::validate(const QVariantMap &ws) const
{
    const QVariantList windows = ws.value(QStringLiteral("windows")).toList();
    if (windows.isEmpty())
        return tr("A workspace needs at least one window.");
    if (windows.first().toMap().value(QStringLiteral("id")).toString() != QLatin1String("main"))
        return tr("The first window must be the main window.");
    QSet<QString> seen;
    for (const QVariant &w : windows) {
        const QString err = validateNode(w.toMap().value(QStringLiteral("root")).toMap(), seen);
        if (!err.isEmpty())
            return err;
    }
    return {};
}

QString WorkspaceStore::saveWorkspace(const QVariantMap &input)
{
    QVariantMap ws = normalizedWorkspace(input);
    const QString err = validate(ws);
    if (!err.isEmpty())
        return err;

    QString id = ws.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        id = newId();
    ws[QStringLiteral("id")] = id;
    if (ws.value(QStringLiteral("name")).toString().trimmed().isEmpty())
        ws[QStringLiteral("name")] = tr("Workspace");

    if (m_presets.contains(id)) {
        ws[QStringLiteral("builtin")] = true;
        ws[QStringLiteral("modified")] = true;
        ws[QStringLiteral("order")] = m_presets.value(id).value(QStringLiteral("order"));
    } else {
        ws[QStringLiteral("builtin")] = false;
        ws.remove(QStringLiteral("modified"));
        ws.remove(QStringLiteral("order"));
    }

    store(ws);
    emit workspacesChanged();
    if (id == m_activeId)
        emit activeWorkspaceEdited();
    return {};
}

QString WorkspaceStore::duplicate(const QString &id)
{
    if (!exists(id))
        return {};
    QVariantMap ws = effective(id);
    const QString copy = newId();
    ws[QStringLiteral("id")] = copy;
    ws[QStringLiteral("name")] = tr("%1 copy").arg(ws.value(QStringLiteral("name")).toString());
    ws[QStringLiteral("builtin")] = false;
    ws.remove(QStringLiteral("modified"));
    ws.remove(QStringLiteral("order"));
    store(ws);
    emit workspacesChanged();
    return copy;
}

bool WorkspaceStore::remove(const QString &id)
{
    const int i = userIndex(id);
    if (i < 0 || m_presets.contains(id))
        return false;
    m_user.removeAt(i);
    persist();
    if (id == m_activeId) {
        m_activeId = QLatin1String(kDefaultId);
        QSettings().setValue(QLatin1String(kActiveKey), m_activeId);
        emit activeIdChanged();
    }
    emit workspacesChanged();
    return true;
}

void WorkspaceStore::resetPreset(const QString &id)
{
    const int i = userIndex(id);
    if (i < 0 || !m_presets.contains(id))
        return;
    m_user.removeAt(i);
    persist();
    emit workspacesChanged();
    if (id == m_activeId)
        emit activeWorkspaceEdited();
}

void WorkspaceStore::rename(const QString &id, const QString &name)
{
    if (!exists(id) || name.trimmed().isEmpty())
        return;
    QVariantMap ws = effective(id);
    ws[QStringLiteral("name")] = name.trimmed();
    if (m_presets.contains(id))
        ws[QStringLiteral("modified")] = true;
    store(ws);
    emit workspacesChanged();
}

bool WorkspaceStore::editQuietly(const QString &id, const std::function<bool(QVariantMap &)> &edit)
{
    if (!exists(id))
        return false;
    QVariantMap ws = effective(id);
    if (!edit(ws))
        return false;
    if (m_presets.contains(id))
        ws[QStringLiteral("modified")] = true;
    store(ws);
    return true;
}

void WorkspaceStore::setSizes(const QString &id, const QString &windowId,
                              const QVariantList &nodePath, const QVariantList &sizes)
{
    editQuietly(id, [&](QVariantMap &ws) {
        QVariantList windows;
        int index = 0;
        if (!findWindow(ws, windowId, windows, index))
            return false;
        QVariantMap window = windows.at(index).toMap();
        QVariantMap root = window.value(QStringLiteral("root")).toMap();
        if (!setNodeSizes(root, nodePath, 0, sizes))
            return false;
        window[QStringLiteral("root")] = root;
        windows[index] = window;
        ws[QStringLiteral("windows")] = windows;
        return true;
    });
}

void WorkspaceStore::setWindowGeometry(const QString &id, const QString &windowId,
                                       const QRect &geometry, const QString &screenName)
{
    editQuietly(id, [&](QVariantMap &ws) {
        QVariantList windows;
        int index = 0;
        if (!findWindow(ws, windowId, windows, index))
            return false;
        QVariantMap window = windows.at(index).toMap();
        window[QStringLiteral("geometry")] = QVariantList{geometry.x(), geometry.y(),
                                                          geometry.width(), geometry.height()};
        window[QStringLiteral("screen")] = screenName;
        windows[index] = window;
        ws[QStringLiteral("windows")] = windows;
        return true;
    });
}


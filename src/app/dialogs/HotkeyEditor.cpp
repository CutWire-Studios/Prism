#include "app/dialogs/HotkeyEditor.h"

#include "app/AppController.h"
#include "app/hotkeys/HotkeyManager.h"

#include <QFile>
#include <QJsonDocument>
#include <QKeySequence>

HotkeyEditor::HotkeyEditor(QObject *parent)
    : QObject(parent)
{
    refresh();
}

HotkeyManager *HotkeyEditor::manager() const
{
    auto *app = AppController::instance();
    return app ? app->hotkeyManager() : nullptr;
}

ClipGraph *HotkeyEditor::graph() const
{
    auto *app = AppController::instance();
    return app ? app->clipGraph() : nullptr;
}

void HotkeyEditor::refresh()
{
    HotkeyManager *mgr = manager();
    ClipGraph *g = graph();
    if (!mgr || !g)
        return;

    if (!m_connected) {
        m_connected = true;
        connect(mgr, &HotkeyManager::bindingsChanged, this, &HotkeyEditor::refresh);
        connect(g, &ClipGraph::changed, this, &HotkeyEditor::refresh);
    }

    const QVector<ClipGraph::AbSlotInfo> inputs = g->abSelectInputs();
    QList<NodeId> abNodes;
    for (const ClipGraph::AbSlotInfo &info : inputs)
        if (!abNodes.contains(info.ref.abNodeId))
            abNodes.append(info.ref.abNodeId);

    m_slots.clear();
    m_rows.clear();
    for (const ClipGraph::AbSlotInfo &info : inputs) {
        QString label = info.name.isEmpty() ? tr("In %1").arg(info.ref.slot + 1) : info.name;
        if (!info.sourceName.isEmpty())
            label += QStringLiteral(" — %1").arg(info.sourceName);
        if (abNodes.size() > 1)
            label = tr("A/B #%1 · %2").arg(abNodes.indexOf(info.ref.abNodeId) + 1).arg(label);

        const Qt::Key key = mgr->bindingForSlot(info.ref);
        QString chord;
        QString deckB;
        if (key != Qt::Key_unknown) {
            chord = QKeySequence(key).toString(QKeySequence::PortableText);
            deckB = QKeySequence(Qt::SHIFT | key).toString(QKeySequence::PortableText);
        }
        m_slots.append(info.ref);
        m_rows.append(QVariantMap{{"label", label}, {"chord", chord}, {"deckBChord", deckB}});
    }
    emit rowsChanged();
}

QVariantMap HotkeyEditor::setChord(int row, const QString &chord)
{
    HotkeyManager *mgr = manager();
    if (!mgr || row < 0 || row >= m_slots.size())
        return {{"ok", false}, {"message", QString()}};

    const QKeySequence seq(chord, QKeySequence::PortableText);
    if (seq.isEmpty() || seq.count() != 1)
        return {{"ok", false}, {"message", tr("That key cannot be used.")}};
    const QKeyCombination combo = seq[0];
    if (combo.keyboardModifiers() != Qt::NoModifier)
        return {{"ok", false}, {"message", tr("Use a key on its own. Shift plus the key always sends to Deck B.")}};

    const Qt::Key key = combo.key();
    if (!HotkeyManager::isBindableKey(key))
        return {{"ok", false}, {"message", tr("That key cannot be used.")}};

    const ClipGraph::AbSlotRef owner = mgr->slotForKey(key);
    if (owner.isValid() && !(owner == m_slots.at(row)))
        return {{"ok", false}, {"message", tr("%1 is already assigned to another input.")
                                               .arg(QKeySequence(key).toString())}};

    if (!mgr->setBinding(m_slots.at(row), key))
        return {{"ok", false}, {"message", tr("That key cannot be used.")}};
    mgr->saveSettingsProfile();
    return {{"ok", true}, {"message", QString()}};
}

void HotkeyEditor::clearRow(int row)
{
    HotkeyManager *mgr = manager();
    if (!mgr || row < 0 || row >= m_slots.size())
        return;
    mgr->clearBinding(m_slots.at(row));
    mgr->saveSettingsProfile();
}

void HotkeyEditor::clearAll()
{
    HotkeyManager *mgr = manager();
    if (!mgr)
        return;
    mgr->applyBindings({});
}

QVariantMap HotkeyEditor::importProfile(const QString &path)
{
    HotkeyManager *mgr = manager();
    QFile file(path);
    if (!mgr || !file.open(QIODevice::ReadOnly))
        return {{"ok", false}, {"message", tr("Could not open file:\n%1").arg(path)}};

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {{"ok", false}, {"message", tr("Invalid profile file:\n%1").arg(err.errorString())}};

    QStringList warnings;
    if (!mgr->importProfile(doc.object(), &warnings))
        return {{"ok", false}, {"message", warnings.isEmpty() ? tr("Import failed.") : warnings.join('\n')}};
    mgr->saveSettingsProfile();
    return {{"ok", true}, {"message", warnings.isEmpty() ? QString()
                                                         : tr("Profile imported with notes:\n%1").arg(warnings.join('\n'))}};
}

QVariantMap HotkeyEditor::exportProfile(const QString &path)
{
    HotkeyManager *mgr = manager();
    QString out = path;
    if (!out.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        out += QStringLiteral(".json");
    QFile file(out);
    if (!mgr || !file.open(QIODevice::WriteOnly))
        return {{"ok", false}, {"message", tr("Could not write file:\n%1").arg(out)}};
    file.write(QJsonDocument(mgr->exportProfile()).toJson(QJsonDocument::Indented));
    return {{"ok", true}, {"message", tr("Hotkey profile saved to:\n%1").arg(out)}};
}

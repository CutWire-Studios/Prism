#include "app/dialogs/HotkeyEditor.h"

#include "app/AppController.h"
#include "app/hotkeys/AppShortcuts.h"
#include "app/hotkeys/HotkeyManager.h"
#include "app/show/ShowManager.h"

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

ShowManager *HotkeyEditor::show() const
{
    auto *app = AppController::instance();
    return app ? app->showManager() : nullptr;
}

void HotkeyEditor::refresh()
{
    HotkeyManager *mgr = manager();
    ShowManager *sm = show();
    if (!mgr || !sm)
        return;

    if (!m_connected) {
        m_connected = true;
        connect(mgr, &HotkeyManager::bindingsChanged, this, &HotkeyEditor::refresh);
        connect(sm, &ShowManager::scenesReset, this, &HotkeyEditor::refresh);
        connect(sm, &ShowManager::sceneAdded, this, &HotkeyEditor::refresh);
        connect(mgr->appShortcuts(), &AppShortcuts::changed, this, &HotkeyEditor::refreshApp);
        refreshApp();
    }

    m_sceneIds.clear();
    m_rows.clear();
    for (const Scene *scene : sm->scenes()) {
        const Qt::Key key = mgr->bindingForScene(scene->id());
        QString chord;
        QString cutChord;
        if (key != Qt::Key_unknown) {
            chord = QKeySequence(key).toString(QKeySequence::PortableText);
            cutChord = QKeySequence(Qt::SHIFT | key).toString(QKeySequence::PortableText);
        }
        m_sceneIds.append(scene->id());
        m_rows.append(QVariantMap{{"label", scene->name()}, {"chord", chord}, {"cutChord", cutChord}});
    }
    emit rowsChanged();
}

void HotkeyEditor::refreshApp()
{
    HotkeyManager *mgr = manager();
    if (!mgr)
        return;
    const AppShortcuts *actions = mgr->appShortcuts();
    m_appSequences = actions->sequenceMap();
    m_appRows.clear();
    for (const AppShortcuts::Action &a : actions->actions()) {
        const QStringList seqs = actions->sequences(a.id);
        m_appRows.append(QVariantMap{{"id", a.id}, {"label", a.label}, {"category", a.category},
                                     {"chord", seqs.value(0)}, {"overridden", actions->isOverridden(a.id)}});
    }
    emit appRowsChanged();
}

QVariantMap HotkeyEditor::setAppChord(const QString &id, const QString &chord)
{
    HotkeyManager *mgr = manager();
    if (!mgr)
        return {{"ok", false}, {"message", QString()}};
    const QString error = mgr->setActionSequence(id, QKeySequence(chord, QKeySequence::PortableText));
    return {{"ok", error.isEmpty()}, {"message", error}};
}

void HotkeyEditor::clearAppChord(const QString &id)
{
    if (HotkeyManager *mgr = manager())
        mgr->clearActionSequence(id);
}

QVariantMap HotkeyEditor::resetAppChord(const QString &id)
{
    HotkeyManager *mgr = manager();
    if (!mgr)
        return {{"ok", false}, {"message", QString()}};
    const QString error = mgr->resetAction(id);
    return {{"ok", error.isEmpty()}, {"message", error}};
}

void HotkeyEditor::resetAllAppChords()
{
    if (HotkeyManager *mgr = manager())
        mgr->resetAllActions();
}

QVariantMap HotkeyEditor::setChord(int row, const QString &chord)
{
    HotkeyManager *mgr = manager();
    if (!mgr || row < 0 || row >= m_sceneIds.size())
        return {{"ok", false}, {"message", QString()}};

    const QKeySequence seq(chord, QKeySequence::PortableText);
    if (seq.isEmpty() || seq.count() != 1)
        return {{"ok", false}, {"message", tr("That key cannot be used.")}};
    const QKeyCombination combo = seq[0];
    if (combo.keyboardModifiers() != Qt::NoModifier)
        return {{"ok", false}, {"message", tr("Use a key on its own. Shift plus the key stages it and takes it to Live.")}};

    const Qt::Key key = combo.key();
    if (!HotkeyManager::isBindableKey(key))
        return {{"ok", false}, {"message", tr("That key cannot be used.")}};

    const QString appLabel = mgr->appShortcuts()->labelUsingPlainKey(key);
    if (!appLabel.isEmpty())
        return {{"ok", false}, {"message", tr("%1 is used by the app shortcut \"%2\".")
                                               .arg(QKeySequence(key).toString(), appLabel)}};

    const QString owner = mgr->sceneForKey(key);
    if (!owner.isEmpty() && owner != m_sceneIds.at(row))
        return {{"ok", false}, {"message", tr("%1 is already assigned to another scene.")
                                               .arg(QKeySequence(key).toString())}};

    if (!mgr->setBinding(m_sceneIds.at(row), key))
        return {{"ok", false}, {"message", tr("That key cannot be used.")}};
    mgr->saveSettingsProfile();
    return {{"ok", true}, {"message", QString()}};
}

void HotkeyEditor::clearRow(int row)
{
    HotkeyManager *mgr = manager();
    if (!mgr || row < 0 || row >= m_sceneIds.size())
        return;
    mgr->clearBinding(m_sceneIds.at(row));
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

#include "app/hotkeys/AppShortcuts.h"

#include <QJsonArray>

namespace {
QStringList standard(QKeySequence::StandardKey key) {
    return AppShortcuts::toPortable(QKeySequence::keyBindings(key));
}

QKeySequence parse(const QString &s) {
    return QKeySequence(s, QKeySequence::PortableText);
}

bool plainOrShift(const QKeyCombination &combo) {
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers() & ~Qt::KeypadModifier;
    return mods == Qt::NoModifier || mods == Qt::ShiftModifier;
}
}

QStringList AppShortcuts::toPortable(const QList<QKeySequence> &seqs) {
    QStringList out;
    for (const QKeySequence &s : seqs) {
        if (!s.isEmpty())
            out.append(s.toString(QKeySequence::PortableText));
    }
    return out;
}

AppShortcuts::AppShortcuts(QObject *parent)
    : QObject(parent)
{
    const QString file = tr("File");
    const QString edit = tr("Edit");
    const QString scene = tr("Scenes");
    const QString panic = tr("Panic");
    const QString transport = tr("Transport");
    m_actions = {
        {QStringLiteral("file.new"), tr("New session"), file, standard(QKeySequence::New)},
        {QStringLiteral("file.open"), tr("Open session"), file, standard(QKeySequence::Open)},
        {QStringLiteral("file.save"), tr("Save session"), file, standard(QKeySequence::Save)},
        {QStringLiteral("file.saveAs"), tr("Save session as"), file, standard(QKeySequence::SaveAs)},
        {QStringLiteral("file.addFiles"), tr("Add media files"), file, {QStringLiteral("Ctrl+Shift+O")}},
        {QStringLiteral("record.marker"), tr("Drop recording marker"), tr("Recording"), {QStringLiteral("Ctrl+M")}},
        {QStringLiteral("edit.undo"), tr("Undo"), edit, standard(QKeySequence::Undo)},
        {QStringLiteral("edit.redo"), tr("Redo"), edit, standard(QKeySequence::Redo)},
        {QStringLiteral("scene.nextTab"), tr("Next scene tab"), scene, {QStringLiteral("Ctrl+Tab")}},
        {QStringLiteral("scene.prevTab"), tr("Previous scene tab"), scene,
         {QStringLiteral("Ctrl+Shift+Tab"), QStringLiteral("Ctrl+Backtab")}},
        {QStringLiteral("scene.goLive"), tr("Go live with edited scene"), scene,
         {QStringLiteral("Ctrl+Return"), QStringLiteral("Ctrl+Enter")}},
        {QStringLiteral("panic.blackout"), tr("Blackout"), panic, {QStringLiteral("B")}},
        {QStringLiteral("panic.freeze"), tr("Freeze"), panic, {QStringLiteral("P")}},
        {QStringLiteral("panic.stayTuned"), tr("Stay tuned"), panic, {QStringLiteral("F9")}},
        {QStringLiteral("transport.cut"), tr("Cut"), transport, {QStringLiteral("Space")}},
        {QStringLiteral("transport.take"), tr("Take"), transport,
         {QStringLiteral("Return"), QStringLiteral("Enter")}},
    };
}

const AppShortcuts::Action *AppShortcuts::action(const QString &id) const {
    for (const Action &a : m_actions) {
        if (a.id == id)
            return &a;
    }
    return nullptr;
}

QStringList AppShortcuts::sequences(const QString &id) const {
    const auto ov = m_overrides.constFind(id);
    if (ov != m_overrides.cend())
        return ov.value();
    const Action *a = action(id);
    return a ? a->defaults : QStringList();
}

QVariantMap AppShortcuts::sequenceMap() const {
    QVariantMap map;
    for (const Action &a : m_actions)
        map.insert(a.id, sequences(a.id));
    return map;
}

QString AppShortcuts::actionUsing(const QKeySequence &seq, const QString &except) const {
    for (const Action &a : m_actions) {
        if (a.id == except)
            continue;
        for (const QString &s : sequences(a.id)) {
            if (parse(s) == seq)
                return a.id;
        }
    }
    return {};
}

QString AppShortcuts::labelUsingPlainKey(Qt::Key key) const {
    for (const Action &a : m_actions) {
        for (const QString &s : sequences(a.id)) {
            const QKeySequence seq = parse(s);
            if (seq.count() == 1 && plainOrShift(seq[0]) && seq[0].key() == key)
                return a.label;
        }
    }
    return {};
}

bool AppShortcuts::usesPlainKey(Qt::Key key) const {
    return !labelUsingPlainKey(key).isEmpty();
}

void AppShortcuts::setSequences(const QString &id, const QStringList &sequences) {
    const Action *a = action(id);
    if (!a)
        return;
    if (sequences == a->defaults)
        m_overrides.remove(id);
    else
        m_overrides.insert(id, sequences);
    emit changed();
}

void AppShortcuts::reset(const QString &id) {
    if (m_overrides.remove(id))
        emit changed();
}

void AppShortcuts::resetAll() {
    if (m_overrides.isEmpty())
        return;
    m_overrides.clear();
    emit changed();
}

QJsonObject AppShortcuts::overridesJson() const {
    QJsonObject obj;
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it)
        obj.insert(it.key(), QJsonArray::fromStringList(it.value()));
    return obj;
}

QStringList AppShortcuts::setOverrides(const QJsonObject &overrides) {
    QMap<QString, QStringList> next;
    for (const Action &a : m_actions) {
        if (!overrides.contains(a.id))
            continue;
        QStringList seqs;
        for (const QJsonValue &v : overrides.value(a.id).toArray()) {
            const QKeySequence seq = parse(v.toString());
            if (seq.count() == 1)
                seqs.append(seq.toString(QKeySequence::PortableText));
        }
        if (seqs != a.defaults)
            next.insert(a.id, seqs);
    }

    // Reverting one override can expose a clash with another, so repeat until stable.
    QStringList reverted;
    bool again = true;
    while (again) {
        again = false;
        QMap<QKeySequence, QString> owner;
        for (const Action &a : m_actions) {
            for (const QString &s : next.value(a.id, a.defaults)) {
                const QKeySequence seq = parse(s);
                const QString other = owner.value(seq);
                if (other.isEmpty()) {
                    owner.insert(seq, a.id);
                    continue;
                }
                const QString victim = next.contains(a.id) ? a.id : other;
                if (!next.remove(victim))
                    continue;
                reverted.append(victim);
                again = true;
                break;
            }
            if (again)
                break;
        }
    }

    m_overrides = next;
    emit changed();
    return reverted;
}

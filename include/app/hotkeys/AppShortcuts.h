#pragma once

#include <QJsonObject>
#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <Qt>

/// Registry of the global app shortcuts (Main.qml): stable id, label, category, default
/// sequences and the user's overrides. Only overrides are persisted, so defaults can change.
/// Cross-checks against scene hotkeys live in HotkeyManager, which owns the instance.
class AppShortcuts : public QObject {
    Q_OBJECT
public:
    struct Action {
        QString id;
        QString label;
        QString category;
        QStringList defaults;   // PortableText
    };

    explicit AppShortcuts(QObject *parent = nullptr);

    const QList<Action> &actions() const { return m_actions; }
    const Action *action(const QString &id) const;

    /// Current sequences (override if present, else defaults), PortableText.
    QStringList sequences(const QString &id) const;
    bool isOverridden(const QString &id) const { return m_overrides.contains(id); }

    /// id -> sequences for every action; the QML-facing form, re-read on `changed`.
    QVariantMap sequenceMap() const;

    /// Id of another action than `except` that currently uses `seq`, or empty.
    QString actionUsing(const QKeySequence &seq, const QString &except = QString()) const;

    /// True if an action has a plain or Shift chord on `key`; scene hotkeys use exactly those.
    bool usesPlainKey(Qt::Key key) const;
    /// Label of the action behind usesPlainKey(), or empty.
    QString labelUsingPlainKey(Qt::Key key) const;

    /// No validation; storing the defaults drops the override.
    void setSequences(const QString &id, const QStringList &sequences);
    void reset(const QString &id);
    void resetAll();

    QJsonObject overridesJson() const;
    /// Replace all overrides. Unknown ids and unparsable entries are ignored; overrides
    /// that would put one chord on two actions are reverted. Returns the reverted ids.
    QStringList setOverrides(const QJsonObject &overrides);

    static QStringList toPortable(const QList<QKeySequence> &seqs);

signals:
    void changed();

private:
    QList<Action> m_actions;
    QMap<QString, QStringList> m_overrides;
};

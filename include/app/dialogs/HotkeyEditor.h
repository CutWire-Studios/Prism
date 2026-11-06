#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "core/graph/ClipGraph.h"

class HotkeyManager;

/// QML-facing view of HotkeyManager's A/B switcher key bindings. Edits apply and persist
/// immediately; a key already used by another input is refused.
class HotkeyEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Rows {label, chord, deckBChord}; chord is empty when unbound.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)

public:
    explicit HotkeyEditor(QObject *parent = nullptr);

    QVariantList rows() const { return m_rows; }

    /// Returns {ok, message}. Only unmodified keys are bindable (Shift+key is always Deck B).
    Q_INVOKABLE QVariantMap setChord(int row, const QString &chord);
    Q_INVOKABLE void clearRow(int row);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE QVariantMap importProfile(const QString &path);
    Q_INVOKABLE QVariantMap exportProfile(const QString &path);
    Q_INVOKABLE void refresh();

signals:
    void rowsChanged();

private:
    HotkeyManager *manager() const;
    ClipGraph *graph() const;

    QList<ClipGraph::AbSlotRef> m_slots;
    QVariantList m_rows;
    bool m_connected = false;
};

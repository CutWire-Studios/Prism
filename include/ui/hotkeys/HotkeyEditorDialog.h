#pragma once

#include <QDialog>
#include <QMap>
#include <Qt>
#include "core/graph/ClipGraph.h"

class HotkeyManager;
class QTableWidget;

/// Visual editor for A/B-switcher-input → hotkey bindings (Deck A / Shift+Deck B).
class HotkeyEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit HotkeyEditorDialog(HotkeyManager *manager, ClipGraph *graph,
                                QWidget *parent = nullptr);

private slots:
    void onImportProfile();
    void onExportProfile();
    void onClearAll();
    void onApply();
    void refreshConflictHighlights();

private:
    void populateTable();
    QMap<ClipGraph::AbSlotRef, Qt::Key> collectBindings(bool *hasConflicts) const;
    ClipGraph::AbSlotRef slotRefForRow(int row) const;

    HotkeyManager   *m_manager = nullptr;
    ClipGraph       *m_editor  = nullptr;
    QTableWidget    *m_table   = nullptr;
};

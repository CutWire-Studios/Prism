#pragma once

#include <QDialog>
#include <QMap>
#include <Qt>
#include "ui/nodes/ClipNodeEditor.h"

class HotkeyManager;
class QTableWidget;

/// Visual editor for A/B-switcher-input → hotkey bindings (Deck A / Shift+Deck B).
class HotkeyEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit HotkeyEditorDialog(HotkeyManager *manager, ClipNodeEditor *editor,
                                QWidget *parent = nullptr);

private slots:
    void onImportProfile();
    void onExportProfile();
    void onClearAll();
    void onApply();
    void refreshConflictHighlights();

private:
    void populateTable();
    QMap<AbSlotRef, Qt::Key> collectBindings(bool *hasConflicts) const;
    AbSlotRef slotRefForRow(int row) const;

    HotkeyManager   *m_manager = nullptr;
    ClipNodeEditor  *m_editor  = nullptr;
    QTableWidget    *m_table   = nullptr;
};

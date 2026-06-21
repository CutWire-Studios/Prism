#pragma once

#include <QDialog>
#include "ui/nodes/ClipNodeEditor.h"

class QWidget;

/// Editor for a group node's members and their arrangement, hosted on the
/// ClipNodeEditor's scene for the given group id.
class GroupEditorDialog : public QDialog {
    Q_OBJECT

public:
    GroupEditorDialog(NodeId groupId, ClipNodeEditor *editor, QWidget *parent = nullptr);

private:
    NodeId m_groupId;
    ClipNodeEditor *m_editor;
    QWidget *m_view = nullptr;
};

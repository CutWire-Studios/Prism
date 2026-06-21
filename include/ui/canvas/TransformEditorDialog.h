#pragma once

#include <QDialog>

class ClipNodeEditor;
class TransformCanvasWidget;
struct ClipItem;

/// Visual editor for the position/size of the clips feeding a context node,
/// using a TransformCanvasWidget; applies the layout back to the ClipNodeEditor.
class TransformEditorDialog : public QDialog {
    Q_OBJECT

public:
    TransformEditorDialog(int contextId, ClipNodeEditor *editor, QWidget *parent = nullptr);

private slots:
    void onApply();
    void onCancel();

private:
    void populateClips();

    int m_contextId;
    ClipNodeEditor *m_editor;
    TransformCanvasWidget *m_canvas;
};

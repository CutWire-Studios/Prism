#pragma once

#include <QDialog>

class ClipNodeEditor;
class TransformCanvasWidget;
struct ClipItem;

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

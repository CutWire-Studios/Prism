#pragma once

#include "core/sources/SourceDescriptor.h"
#include "core/text/ShapeStyle.h"
#include <QDialog>
#include <QElapsedTimer>

class QLabel;
class QListWidget;
class QTimer;

namespace style {
class ShadingLayerStackEditor;
class SliderSpinRow;
}

/// Editor for a Shape source: shape gallery, geometry knobs and the shading layer stack, with a
/// live preview rendered by the same code as program output.
class ShapeEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShapeEditDialog(const SourceDescriptor &initial = SourceDescriptor(), QWidget *parent = nullptr);

    SourceDescriptor resultDescriptor() const;

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void selectKind(prism::ShapeKind kind);
    void syncGeometryRows();
    void scheduleRender();
    void updatePreview();
    void updateClock();

    SourceDescriptor m_initial;
    prism::ShapeStyle m_style;

    QListWidget *m_gallery;
    QLabel *m_preview;
    style::ShadingLayerStackEditor *m_layers;
    style::SliderSpinRow *m_cornerRadius, *m_points, *m_innerRatio, *m_headSize, *m_thickness, *m_tailX, *m_tailSize;
    QWidget *m_starBox, *m_arrowBox, *m_shaftBox, *m_bubbleBox;
    QTimer *m_renderTimer;
    QTimer *m_clockTimer;
    QElapsedTimer m_clock;
};

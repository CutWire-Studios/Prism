#pragma once

#include "core/text/TextShading.h"

#include <QList>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace style {

class ColorButton;
class EffectParamsEditor;
class GradientEditor;
class SliderSpinRow;

/// Editor for an ordered stack of prism::TextShadingLayer (a ShapeStyle's or TextStyle's `layers`):
/// a list (front-most on top) with enable toggle, add / duplicate / reorder / delete, and a details
/// panel that shows only the fields the selected layer's kind uses.
class ShadingLayerStackEditor : public QWidget {
    Q_OBJECT
public:
    explicit ShadingLayerStackEditor(QWidget *parent = nullptr);

    /// layers[0] is drawn first (back-most), as in the model.
    const QList<prism::TextShadingLayer> &layers() const { return m_layers; }
    /// Silent: does not emit changed(). Keeps the selection when the row still exists.
    void setLayers(const QList<prism::TextShadingLayer> &layers);

    /// Show the all / base / accent scope field (text only; off by default).
    void setScopeVisible(bool visible);
    /// Show the gradient "Map to" field (text only; off by default).
    void setGradientSpaceVisible(bool visible);

    /// Index into layers(), -1 when nothing is selected.
    int currentLayerIndex() const;
    void setCurrentLayerIndex(int index);
    /// Appends a new layer of this kind on top and selects it; emits changed().
    void addLayer(prism::TextLayerKind kind);

signals:
    void changed();

private:
    void rebuildList(int select);
    void updateItemText(int layerIndex);
    void loadDetails();
    void edit(const std::function<void(prism::TextShadingLayer &)> &fn);
    void move(int delta);
    void duplicateCurrent();
    void removeCurrent();
    void onItemChanged(QListWidgetItem *item);
    int rowToIndex(int row) const { return int(m_layers.size()) - 1 - row; }

    QList<prism::TextShadingLayer> m_layers;
    bool m_loading = false;

    QListWidget *m_list;
    QToolButton *m_add;
    QPushButton *m_dup, *m_up, *m_down, *m_del;
    QWidget *m_details;

    QWidget *m_paintBox, *m_colorRow, *m_textureBox, *m_strokeBox, *m_fillBox, *m_sketchBox, *m_shadowBox,
        *m_softBox, *m_extrudeBox, *m_scopeRow, *m_dashOffsetRow;
    QComboBox *m_paintKind, *m_blend, *m_align, *m_dash, *m_scope;
    ColorButton *m_color;
    GradientEditor *m_gradient;
    EffectParamsEditor *m_effect;
    QPushButton *m_texturePath;
    QCheckBox *m_tile, *m_knockout;
    QSpinBox *m_seed, *m_steps;
    SliderSpinRow *m_texScale, *m_texAngle, *m_texX, *m_texY, *m_opacity, *m_width, *m_dashOffset, *m_trimStart,
        *m_trimEnd, *m_sketchLength, *m_sketchDev, *m_offX, *m_offY, *m_blur, *m_spread, *m_depth, *m_exAngle,
        *m_darken;
};

} // namespace style

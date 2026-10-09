#pragma once

#include "core/text/TextShading.h"

#include <QWidget>

class QComboBox;
class QVBoxLayout;

namespace style {

/// Effect picker plus sliders / colour buttons generated from prism::textShaderEffectSpecs().
class EffectParamsEditor : public QWidget {
    Q_OBJECT
public:
    explicit EffectParamsEditor(QWidget *parent = nullptr);

    const prism::TextShaderEffect &effect() const { return m_effect; }
    /// Silent: does not emit changed(). An empty / unknown id selects the first effect.
    void setEffect(const prism::TextShaderEffect &effect);

signals:
    void changed();

private:
    void rebuildParams();

    prism::TextShaderEffect m_effect;
    QComboBox *m_combo;
    QWidget *m_params = nullptr;
    QVBoxLayout *m_layout;
    bool m_loading = false;
};

} // namespace style

#include "ui/editors/style/EffectParamsEditor.h"

#include "ui/editors/style/StyleControls.h"

#include <QCheckBox>
#include <QComboBox>
#include <QVBoxLayout>

namespace style {

using prism::TextAnimParamSpec;
using prism::VectorSlotValue;

EffectParamsEditor::EffectParamsEditor(QWidget *parent)
    : QWidget(parent)
    , m_combo(new QComboBox(this))
    , m_layout(new QVBoxLayout(this))
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(6);
    for (const prism::TextEffectSpec &spec : prism::textShaderEffectSpecs())
        m_combo->addItem(spec.label, spec.id);
    m_layout->addWidget(labeledRow(tr("Effect"), m_combo));

    connect(m_combo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading)
            return;
        m_effect.id = m_combo->currentData().toString();
        m_effect.params.clear();
        rebuildParams();
        emit changed();
    });
    setEffect({});
}

void EffectParamsEditor::setEffect(const prism::TextShaderEffect &effect)
{
    m_effect = effect;
    if (!prism::textShaderEffectSpec(m_effect.id) && m_combo->count() > 0) {
        m_effect.id = m_combo->itemData(0).toString();
        m_effect.params.clear();
    }
    m_loading = true;
    m_combo->setCurrentIndex(m_combo->findData(m_effect.id));
    m_loading = false;
    rebuildParams();
}

void EffectParamsEditor::rebuildParams()
{
    delete m_params;
    m_params = new QWidget(this);
    auto *lay = new QVBoxLayout(m_params);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    m_layout->addWidget(m_params);

    const prism::TextEffectSpec *spec = prism::textShaderEffectSpec(m_effect.id);
    if (!spec)
        return;

    for (const TextAnimParamSpec &p : spec->params) {
        if (!m_effect.params.contains(p.id))
            m_effect.params.insert(p.id, p.defaultValue);
        const QString id = p.id;
        const QString label = p.label;
        switch (p.type) {
        case TextAnimParamSpec::Type::Scalar: {
            int decimals = 2;
            if (p.step >= 1.0)
                decimals = 0;
            else if (p.step < 0.01)
                decimals = 3;
            auto *row = new SliderSpinRow(label, p.min, p.max, p.step > 0 ? p.step : 0.01, decimals, p.unit, m_params);
            row->setValue(m_effect.params.value(id).scalar);
            connect(row, &SliderSpinRow::valueChanged, this, [this, id](double v) {
                m_effect.params[id] = VectorSlotValue::fromScalar(v);
                emit changed();
            });
            lay->addWidget(row);
            break;
        }
        case TextAnimParamSpec::Type::Color: {
            auto *btn = new ColorButton(m_params);
            btn->setColor(m_effect.params.value(id).color);
            connect(btn, &ColorButton::colorChanged, this, [this, id](const QColor &c) {
                m_effect.params[id] = VectorSlotValue::fromColor(c);
                emit changed();
            });
            lay->addWidget(labeledRow(label, btn, m_params));
            break;
        }
        case TextAnimParamSpec::Type::Bool: {
            auto *box = new QCheckBox(label, m_params);
            box->setChecked(m_effect.params.value(id).scalar != 0.0);
            connect(box, &QCheckBox::toggled, this, [this, id](bool on) {
                m_effect.params[id] = VectorSlotValue::fromScalar(on ? 1.0 : 0.0);
                emit changed();
            });
            lay->addWidget(box);
            break;
        }
        case TextAnimParamSpec::Type::Enum: {
            auto *combo = new QComboBox(m_params);
            combo->addItems(p.enumValues);
            combo->setCurrentText(m_effect.params.value(id).text);
            connect(combo, &QComboBox::currentTextChanged, this, [this, id](const QString &t) {
                m_effect.params[id] = VectorSlotValue::fromText(t);
                emit changed();
            });
            lay->addWidget(labeledRow(label, combo, m_params));
            break;
        }
        case TextAnimParamSpec::Type::Vec2:
        case TextAnimParamSpec::Type::Text:
            break;
        }
    }
}

} // namespace style

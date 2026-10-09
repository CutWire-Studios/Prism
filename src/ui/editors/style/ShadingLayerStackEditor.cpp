#include "ui/editors/style/ShadingLayerStackEditor.h"

#include "ui/common/Icons.h"
#include "ui/editors/style/EffectParamsEditor.h"
#include "ui/editors/style/GradientEditor.h"
#include "ui/editors/style/StyleControls.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace style {

using namespace prism;

namespace {

QString paintLabel(TextPaintKind kind)
{
    switch (kind) {
    case TextPaintKind::Solid: return QObject::tr("Solid");
    case TextPaintKind::Gradient: return QObject::tr("Gradient");
    case TextPaintKind::Texture: return QObject::tr("Texture");
    case TextPaintKind::Effect: return QObject::tr("Effect");
    }
    return {};
}

QString layerText(const TextShadingLayer &l)
{
    QString text = shadingLayerKindLabel(l.kind);
    if (l.kind == TextLayerKind::Fill || l.kind == TextLayerKind::Stroke)
        text += QStringLiteral(" · ") + paintLabel(l.paint.kind);
    return text;
}

QWidget *group(QWidget *parent, QVBoxLayout **out)
{
    auto *w = new QWidget(parent);
    *out = new QVBoxLayout(w);
    (*out)->setContentsMargins(0, 0, 0, 0);
    (*out)->setSpacing(6);
    return w;
}

QLabel *header(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("subHeader"));
    return l;
}

} // namespace

ShadingLayerStackEditor::ShadingLayerStackEditor(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto *titleRow = new QHBoxLayout;
    auto *title = new QLabel(tr("LAYERS"), this);
    title->setObjectName(QStringLiteral("sectionHeader"));
    titleRow->addWidget(title, 1);
    m_add = new QToolButton(this);
    m_add->setText(tr("Add layer"));
    m_add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_add->setPopupMode(QToolButton::InstantPopup);
    m_add->setIcon(Icons::icon(Icons::Names::Add));
    auto *menu = new QMenu(m_add);
    for (TextLayerKind kind : {TextLayerKind::Fill, TextLayerKind::Stroke, TextLayerKind::Shadow, TextLayerKind::Glow,
                               TextLayerKind::Extrude}) {
        menu->addAction(shadingLayerKindLabel(kind), this, [this, kind] { addLayer(kind); });
    }
    m_add->setMenu(menu);
    titleRow->addWidget(m_add);
    root->addLayout(titleRow);

    m_list = new QListWidget(this);
    m_list->setMinimumHeight(112);
    m_list->setMaximumHeight(160);
    root->addWidget(m_list);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);
    m_dup = new QPushButton(tr("Duplicate"), this);
    m_up = new QPushButton(this);
    m_up->setIcon(Icons::icon(Icons::Names::ExpandLess));
    m_up->setToolTip(tr("Bring forward"));
    m_down = new QPushButton(this);
    m_down->setIcon(Icons::icon(Icons::Names::ExpandMore));
    m_down->setToolTip(tr("Send backward"));
    m_del = new QPushButton(this);
    m_del->setIcon(Icons::icon(Icons::Names::Delete));
    m_del->setToolTip(tr("Delete layer"));
    btnRow->addWidget(m_dup);
    btnRow->addStretch(1);
    btnRow->addWidget(m_up);
    btnRow->addWidget(m_down);
    btnRow->addWidget(m_del);
    root->addLayout(btnRow);

    m_details = new QWidget(this);
    auto *dl = new QVBoxLayout(m_details);
    dl->setContentsMargins(0, 4, 0, 0);
    dl->setSpacing(8);
    root->addWidget(m_details);

    QVBoxLayout *l = nullptr;

    // Paint (fill / stroke)
    m_paintBox = group(m_details, &l);
    m_paintKind = new QComboBox(m_paintBox);
    for (TextPaintKind k : {TextPaintKind::Solid, TextPaintKind::Gradient, TextPaintKind::Texture, TextPaintKind::Effect})
        m_paintKind->addItem(paintLabel(k), int(k));
    l->addWidget(labeledRow(tr("Paint"), m_paintKind));
    m_gradient = new GradientEditor(m_paintBox);
    l->addWidget(m_gradient);
    m_textureBox = group(m_paintBox, &l);
    m_texturePath = new QPushButton(tr("Choose image…"), m_textureBox);
    l->addWidget(labeledRow(tr("Image"), m_texturePath));
    m_texScale = new SliderSpinRow(tr("Scale"), 0.05, 8, 0.05, 2, QString(), m_textureBox);
    m_texAngle = new SliderSpinRow(tr("Angle"), -360, 360, 1, 0, QStringLiteral("°"), m_textureBox);
    m_texX = new SliderSpinRow(tr("Offset X"), -2000, 2000, 1, 0, QStringLiteral("px"), m_textureBox);
    m_texY = new SliderSpinRow(tr("Offset Y"), -2000, 2000, 1, 0, QStringLiteral("px"), m_textureBox);
    m_tile = new QCheckBox(tr("Tile"), m_textureBox);
    for (QWidget *w : {static_cast<QWidget *>(m_texScale), static_cast<QWidget *>(m_texAngle), static_cast<QWidget *>(m_texX),
                       static_cast<QWidget *>(m_texY), static_cast<QWidget *>(m_tile)})
        l->addWidget(w);
    static_cast<QVBoxLayout *>(m_paintBox->layout())->addWidget(m_textureBox);
    m_effect = new EffectParamsEditor(m_paintBox);
    static_cast<QVBoxLayout *>(m_paintBox->layout())->addWidget(m_effect);
    dl->addWidget(m_paintBox);

    m_color = new ColorButton(m_details);
    m_colorRow = labeledRow(tr("Colour"), m_color, m_details);
    dl->addWidget(m_colorRow);

    // Common
    m_opacity = new SliderSpinRow(tr("Opacity"), 0, 1, 0.01, 2, QString(), m_details);
    dl->addWidget(m_opacity);
    m_blend = new QComboBox(m_details);
    for (BlendMode b : {BlendMode::Normal, BlendMode::Multiply, BlendMode::Screen, BlendMode::Overlay, BlendMode::Add,
                        BlendMode::Darken, BlendMode::Lighten}) {
        QString name = blendModeToString(b);
        name[0] = name[0].toUpper();
        m_blend->addItem(name, int(b));
    }
    dl->addWidget(labeledRow(tr("Blend"), m_blend));

    // Fill
    m_fillBox = group(m_details, &l);
    m_knockout = new QCheckBox(tr("Knockout (punch through layers beneath)"), m_fillBox);
    l->addWidget(m_knockout);
    dl->addWidget(m_fillBox);

    // Stroke
    m_strokeBox = group(m_details, &l);
    m_width = new SliderSpinRow(tr("Width"), 0, 100, 0.5, 1, QStringLiteral("px"), m_strokeBox);
    l->addWidget(m_width);
    m_align = new QComboBox(m_strokeBox);
    m_align->addItem(tr("Center"), int(StrokeAlign::Center));
    m_align->addItem(tr("Outside"), int(StrokeAlign::Outside));
    m_align->addItem(tr("Inside"), int(StrokeAlign::Inside));
    l->addWidget(labeledRow(tr("Placement"), m_align));
    m_dash = new QComboBox(m_strokeBox);
    m_dash->addItem(tr("Solid"), int(StrokeDash::Solid));
    m_dash->addItem(tr("Dash"), int(StrokeDash::Dash));
    m_dash->addItem(tr("Dot"), int(StrokeDash::Dot));
    m_dash->addItem(tr("Dash dot"), int(StrokeDash::DashDot));
    l->addWidget(labeledRow(tr("Dash"), m_dash));
    m_dashOffset = new SliderSpinRow(tr("Dash offset"), 0, 20, 0.1, 1, QString(), m_strokeBox);
    m_dashOffsetRow = m_dashOffset;
    l->addWidget(m_dashOffset);
    m_trimStart = new SliderSpinRow(tr("Trim start"), 0, 1, 0.01, 2, QString(), m_strokeBox);
    m_trimEnd = new SliderSpinRow(tr("Trim end"), 0, 1, 0.01, 2, QString(), m_strokeBox);
    l->addWidget(m_trimStart);
    l->addWidget(m_trimEnd);
    dl->addWidget(m_strokeBox);

    // Sketch (fill / stroke)
    m_sketchBox = group(m_details, &l);
    l->addWidget(header(tr("Sketchy"), m_sketchBox));
    m_sketchLength = new SliderSpinRow(tr("Length"), 0, 60, 0.5, 1, QStringLiteral("px"), m_sketchBox);
    m_sketchDev = new SliderSpinRow(tr("Deviation"), 0, 30, 0.5, 1, QStringLiteral("px"), m_sketchBox);
    m_seed = new QSpinBox(m_sketchBox);
    m_seed->setRange(0, 9999);
    m_seed->setKeyboardTracking(false);
    l->addWidget(m_sketchLength);
    l->addWidget(m_sketchDev);
    l->addWidget(labeledRow(tr("Seed"), m_seed));
    dl->addWidget(m_sketchBox);

    // Shadow offsets, shadow / glow softness
    m_shadowBox = group(m_details, &l);
    m_offX = new SliderSpinRow(tr("Offset X"), -500, 500, 1, 0, QStringLiteral("px"), m_shadowBox);
    m_offY = new SliderSpinRow(tr("Offset Y"), -500, 500, 1, 0, QStringLiteral("px"), m_shadowBox);
    l->addWidget(m_offX);
    l->addWidget(m_offY);
    dl->addWidget(m_shadowBox);

    m_softBox = group(m_details, &l);
    m_blur = new SliderSpinRow(tr("Blur"), 0, 200, 0.5, 1, QStringLiteral("px"), m_softBox);
    m_spread = new SliderSpinRow(tr("Spread"), 0, 100, 0.5, 1, QStringLiteral("px"), m_softBox);
    l->addWidget(m_blur);
    l->addWidget(m_spread);
    dl->addWidget(m_softBox);

    // Extrude
    m_extrudeBox = group(m_details, &l);
    m_depth = new SliderSpinRow(tr("Depth"), 0, 100, 0.5, 1, QStringLiteral("px"), m_extrudeBox);
    m_exAngle = new SliderSpinRow(tr("Angle"), -360, 360, 5, 0, QStringLiteral("°"), m_extrudeBox);
    m_steps = new QSpinBox(m_extrudeBox);
    m_steps->setRange(1, 64);
    m_steps->setKeyboardTracking(false);
    m_darken = new SliderSpinRow(tr("Darken"), 0, 1, 0.01, 2, QString(), m_extrudeBox);
    l->addWidget(m_depth);
    l->addWidget(m_exAngle);
    l->addWidget(labeledRow(tr("Steps"), m_steps));
    l->addWidget(m_darken);
    dl->addWidget(m_extrudeBox);

    m_scope = new QComboBox(m_details);
    m_scope->addItem(tr("All text"), int(TextLayerScope::All));
    m_scope->addItem(tr("Base only"), int(TextLayerScope::Base));
    m_scope->addItem(tr("Accent only"), int(TextLayerScope::Accent));
    m_scopeRow = labeledRow(tr("Applies to"), m_scope, m_details);
    m_scopeRow->setVisible(false);
    dl->addWidget(m_scopeRow);

    connect(m_list, &QListWidget::currentRowChanged, this, [this] { loadDetails(); });
    connect(m_list, &QListWidget::itemChanged, this, &ShadingLayerStackEditor::onItemChanged);
    connect(m_dup, &QPushButton::clicked, this, &ShadingLayerStackEditor::duplicateCurrent);
    connect(m_del, &QPushButton::clicked, this, &ShadingLayerStackEditor::removeCurrent);
    connect(m_up, &QPushButton::clicked, this, [this] { move(+1); });
    connect(m_down, &QPushButton::clicked, this, [this] { move(-1); });

    connect(m_paintKind, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading)
            return;
        const auto kind = TextPaintKind(m_paintKind->currentData().toInt());
        edit([this, kind](TextShadingLayer &l) {
            l.paint.kind = kind;
            if (kind == TextPaintKind::Effect && l.paint.effect.id.isEmpty()) {
                m_effect->setEffect({});
                l.paint.effect = m_effect->effect();
            }
        });
        updateItemText(currentLayerIndex());
        loadDetails();
    });
    connect(m_color, &ColorButton::colorChanged, this, [this](const QColor &c) {
        edit([c](TextShadingLayer &l) { l.paint.color = c; });
    });
    connect(m_gradient, &GradientEditor::changed, this, [this] {
        const TextGradient g = m_gradient->gradient();
        edit([g](TextShadingLayer &l) { l.paint.gradient = g; });
    });
    connect(m_effect, &EffectParamsEditor::changed, this, [this] {
        const TextShaderEffect e = m_effect->effect();
        edit([e](TextShadingLayer &l) { l.paint.effect = e; });
    });
    connect(m_texturePath, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose image"), QString(),
                                                          tr("Images (*.png *.jpg *.jpeg *.webp *.bmp);;All files (*)"));
        if (path.isEmpty())
            return;
        edit([path](TextShadingLayer &l) { l.paint.texture.path = path; });
        loadDetails();
    });
    connect(m_texScale, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.paint.texture.scale = v; }); });
    connect(m_texAngle, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.paint.texture.angle = v; }); });
    connect(m_texX, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.paint.texture.offset.setX(v); }); });
    connect(m_texY, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.paint.texture.offset.setY(v); }); });
    connect(m_tile, &QCheckBox::toggled, this, [this](bool on) { edit([on](auto &l) { l.paint.texture.tile = on; }); });

    connect(m_opacity, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.opacity = v; }); });
    connect(m_blend, &QComboBox::currentIndexChanged, this, [this] {
        const auto b = BlendMode(m_blend->currentData().toInt());
        edit([b](auto &l) { l.blend = b; });
    });
    connect(m_knockout, &QCheckBox::toggled, this, [this](bool on) { edit([on](auto &l) { l.knockout = on; }); });
    connect(m_width, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.width = v; }); });
    connect(m_depth, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.width = v; }); });
    connect(m_align, &QComboBox::currentIndexChanged, this, [this] {
        const auto a = StrokeAlign(m_align->currentData().toInt());
        edit([a](auto &l) { l.strokeAlign = a; });
    });
    connect(m_dash, &QComboBox::currentIndexChanged, this, [this] {
        const auto d = StrokeDash(m_dash->currentData().toInt());
        edit([d](auto &l) { l.dash = d; });
        m_dashOffsetRow->setVisible(d != StrokeDash::Solid);
    });
    connect(m_dashOffset, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.dashOffset = v; }); });
    connect(m_trimStart, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.trimStart = v; }); });
    connect(m_trimEnd, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.trimEnd = v; }); });
    connect(m_sketchLength, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.sketchLength = v; }); });
    connect(m_sketchDev, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.sketchDeviation = v; }); });
    connect(m_seed, &QSpinBox::valueChanged, this, [this](int v) { edit([v](auto &l) { l.sketchSeed = v; }); });
    connect(m_offX, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.offsetX = v; }); });
    connect(m_offY, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.offsetY = v; }); });
    connect(m_blur, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.blur = v; }); });
    connect(m_spread, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.spread = v; }); });
    connect(m_exAngle, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.extrudeAngle = v; }); });
    connect(m_steps, &QSpinBox::valueChanged, this, [this](int v) { edit([v](auto &l) { l.extrudeSteps = v; }); });
    connect(m_darken, &SliderSpinRow::valueChanged, this, [this](double v) { edit([v](auto &l) { l.extrudeDarken = v; }); });
    connect(m_scope, &QComboBox::currentIndexChanged, this, [this] {
        const auto s = TextLayerScope(m_scope->currentData().toInt());
        edit([s](auto &l) { l.scope = s; });
    });

    loadDetails();
}

void ShadingLayerStackEditor::setLayers(const QList<TextShadingLayer> &layers)
{
    const int keep = currentLayerIndex();
    m_layers = layers;
    rebuildList(keep >= 0 && keep < m_layers.size() ? keep : int(m_layers.size()) - 1);
}

void ShadingLayerStackEditor::setScopeVisible(bool visible)
{
    m_scopeRow->setProperty("wanted", visible);
    loadDetails();
}

void ShadingLayerStackEditor::setGradientSpaceVisible(bool visible)
{
    m_gradient->setSpaceVisible(visible);
}

int ShadingLayerStackEditor::currentLayerIndex() const
{
    const int row = m_list->currentRow();
    return row < 0 ? -1 : rowToIndex(row);
}

void ShadingLayerStackEditor::setCurrentLayerIndex(int index)
{
    if (index >= 0 && index < m_layers.size())
        m_list->setCurrentRow(int(m_layers.size()) - 1 - index);
}

void ShadingLayerStackEditor::addLayer(TextLayerKind kind)
{
    TextShadingLayer layer;
    const QString id = mintTextLayerId(m_layers);
    switch (kind) {
    case TextLayerKind::Fill: layer = solidFillLayer(Qt::white, id); break;
    case TextLayerKind::Stroke: layer = strokeLayer(4.0, Qt::white, id); break;
    case TextLayerKind::Shadow: layer = shadowLayer(QColor(0, 0, 0, 200), 6.0, 6.0, 12.0, 0.8, id); break;
    case TextLayerKind::Glow: layer = glowLayer(QColor(0, 180, 255), 16.0, 0.9, id); break;
    case TextLayerKind::Extrude:
        layer = solidFillLayer(QColor(30, 30, 30), id);
        layer.kind = TextLayerKind::Extrude;
        layer.width = 10.0;
        break;
    }
    m_layers.append(layer);
    rebuildList(int(m_layers.size()) - 1);
    emit changed();
}

void ShadingLayerStackEditor::rebuildList(int select)
{
    m_loading = true;
    m_list->clear();
    for (int row = 0; row < m_layers.size(); ++row) {
        const TextShadingLayer &l = m_layers[rowToIndex(row)];
        auto *item = new QListWidgetItem(layerText(l), m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(l.enabled ? Qt::Checked : Qt::Unchecked);
    }
    if (select >= 0 && select < m_layers.size())
        m_list->setCurrentRow(int(m_layers.size()) - 1 - select);
    m_loading = false;
    loadDetails();
}

void ShadingLayerStackEditor::updateItemText(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= m_layers.size())
        return;
    const bool was = m_loading;
    m_loading = true;
    m_list->item(int(m_layers.size()) - 1 - layerIndex)->setText(layerText(m_layers[layerIndex]));
    m_loading = was;
}

void ShadingLayerStackEditor::onItemChanged(QListWidgetItem *item)
{
    if (m_loading)
        return;
    const int idx = rowToIndex(m_list->row(item));
    if (idx < 0 || idx >= m_layers.size())
        return;
    m_layers[idx].enabled = item->checkState() == Qt::Checked;
    emit changed();
}

void ShadingLayerStackEditor::edit(const std::function<void(TextShadingLayer &)> &fn)
{
    const int idx = currentLayerIndex();
    if (m_loading || idx < 0)
        return;
    fn(m_layers[idx]);
    emit changed();
}

void ShadingLayerStackEditor::move(int delta)
{
    const int idx = currentLayerIndex();
    const int to = idx + delta;
    if (idx < 0 || to < 0 || to >= m_layers.size())
        return;
    m_layers.swapItemsAt(idx, to);
    rebuildList(to);
    emit changed();
}

void ShadingLayerStackEditor::duplicateCurrent()
{
    const int idx = currentLayerIndex();
    if (idx < 0)
        return;
    TextShadingLayer copy = m_layers[idx];
    copy.id = mintTextLayerId(m_layers);
    m_layers.insert(idx + 1, copy);
    rebuildList(idx + 1);
    emit changed();
}

void ShadingLayerStackEditor::removeCurrent()
{
    const int idx = currentLayerIndex();
    if (idx < 0)
        return;
    m_layers.removeAt(idx);
    rebuildList(std::min(idx, int(m_layers.size()) - 1));
    emit changed();
}

void ShadingLayerStackEditor::loadDetails()
{
    const int idx = currentLayerIndex();
    const bool has = idx >= 0 && idx < m_layers.size();
    m_details->setVisible(has);
    m_dup->setEnabled(has);
    m_del->setEnabled(has);
    m_up->setEnabled(has && idx < m_layers.size() - 1);
    m_down->setEnabled(has && idx > 0);
    if (!has)
        return;

    const TextShadingLayer &l = m_layers[idx];
    const bool wasLoading = m_loading;
    m_loading = true;

    const bool fill = l.kind == TextLayerKind::Fill;
    const bool stroke = l.kind == TextLayerKind::Stroke;
    const bool painted = fill || stroke;
    const TextPaintKind pk = painted ? l.paint.kind : TextPaintKind::Solid;

    m_paintBox->setVisible(painted);
    m_paintKind->setCurrentIndex(m_paintKind->findData(int(l.paint.kind)));
    m_gradient->setVisible(pk == TextPaintKind::Gradient);
    m_gradient->setGradient(l.paint.gradient);
    m_textureBox->setVisible(pk == TextPaintKind::Texture);
    m_texturePath->setText(l.paint.texture.path.isEmpty() ? tr("Choose image…") : QFileInfo(l.paint.texture.path).fileName());
    m_texScale->setValue(l.paint.texture.scale);
    m_texAngle->setValue(l.paint.texture.angle);
    m_texX->setValue(l.paint.texture.offset.x());
    m_texY->setValue(l.paint.texture.offset.y());
    m_tile->setChecked(l.paint.texture.tile);
    m_effect->setVisible(pk == TextPaintKind::Effect);
    m_effect->setEffect(l.paint.effect);

    m_colorRow->setVisible(pk == TextPaintKind::Solid || pk == TextPaintKind::Texture || pk == TextPaintKind::Effect);
    m_color->setColor(l.paint.color);
    if (auto *label = m_colorRow->findChild<QLabel *>())
        label->setText(pk == TextPaintKind::Solid ? tr("Colour") : tr("Tint"));

    m_opacity->setValue(l.opacity);
    m_blend->setCurrentIndex(m_blend->findData(int(l.blend)));

    m_fillBox->setVisible(fill);
    m_knockout->setChecked(l.knockout);

    m_strokeBox->setVisible(stroke);
    m_width->setValue(l.width);
    m_align->setCurrentIndex(m_align->findData(int(l.strokeAlign)));
    m_dash->setCurrentIndex(m_dash->findData(int(l.dash)));
    m_dashOffset->setValue(l.dashOffset);
    m_dashOffsetRow->setVisible(l.dash != StrokeDash::Solid);
    m_trimStart->setValue(l.trimStart);
    m_trimEnd->setValue(l.trimEnd);

    m_sketchBox->setVisible(painted);
    m_sketchLength->setValue(l.sketchLength);
    m_sketchDev->setValue(l.sketchDeviation);
    m_seed->setValue(l.sketchSeed);

    m_shadowBox->setVisible(l.kind == TextLayerKind::Shadow);
    m_offX->setValue(l.offsetX);
    m_offY->setValue(l.offsetY);
    m_softBox->setVisible(l.kind == TextLayerKind::Shadow || l.kind == TextLayerKind::Glow);
    m_blur->setValue(l.blur);
    m_spread->setValue(l.spread);

    m_extrudeBox->setVisible(l.kind == TextLayerKind::Extrude);
    m_depth->setValue(l.width);
    m_exAngle->setValue(l.extrudeAngle);
    m_steps->setValue(l.extrudeSteps);
    m_darken->setValue(l.extrudeDarken);

    m_scopeRow->setVisible(m_scopeRow->property("wanted").toBool());
    m_scope->setCurrentIndex(m_scope->findData(int(l.scope)));

    m_loading = wasLoading;
}

} // namespace style

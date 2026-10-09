#include "ui/canvas/TransformEditorDialog.h"
#include "ui/canvas/TransformCanvasWidget.h"
#include "ui/nodes/ClipNodeEditor.h"
#include "ui/common/Icons.h"
#include "ui/common/Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QSpinBox>
#include <QComboBox>

TransformEditorDialog::TransformEditorDialog(int contextId, ClipNodeEditor *editor, QWidget *parent)
    : QDialog(parent), m_contextId(contextId), m_editor(editor)
{
    setWindowTitle("Layer Layout");
    setMinimumSize(940, 600);
    resize(1200, 760);
    setModal(true);

    m_editor->layerCanvasSize((NodeId)m_contextId, m_origCanvasW, m_origCanvasH);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    // ── Header: title + canvas size ─────────────────────────────────────────
    auto *header = new QHBoxLayout();
    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(1);
    auto *title = new QLabel("Layer Layout");
    title->setStyleSheet("font-size: 15px; font-weight: bold;");
    auto *subtitle = new QLabel("Changes apply to the output in real time");
    subtitle->setProperty("role", "secondary");
    subtitle->setStyleSheet("font-size: 11px;");
    titleCol->addWidget(title);
    titleCol->addWidget(subtitle);
    header->addLayout(titleCol);
    header->addStretch();

    auto *canvasLabel = new QLabel("Canvas");
    canvasLabel->setProperty("role", "secondary");
    canvasLabel->setStyleSheet("font-size: 11px; font-weight: bold;");
    m_canvasWSpin = new QSpinBox();
    m_canvasHSpin = new QSpinBox();
    for (QSpinBox *s : { m_canvasWSpin, m_canvasHSpin }) {
        s->setRange(16, 8192);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setFixedWidth(64);
        s->setAlignment(Qt::AlignCenter);
    }
    m_canvasWSpin->setValue(m_origCanvasW);
    m_canvasHSpin->setValue(m_origCanvasH);
    auto *presetCombo = new QComboBox();
    presetCombo->addItem("Preset…");
    presetCombo->addItem("1280 × 720",  QSize(1280, 720));
    presetCombo->addItem("1920 × 1080", QSize(1920, 1080));
    presetCombo->addItem("3840 × 2160", QSize(3840, 2160));
    presetCombo->addItem("1080 × 1920", QSize(1080, 1920));
    presetCombo->addItem("1024 × 768",  QSize(1024, 768));
    header->addWidget(canvasLabel);
    header->addSpacing(6);
    header->addWidget(m_canvasWSpin);
    header->addWidget(new QLabel("×"));
    header->addWidget(m_canvasHSpin);
    header->addSpacing(6);
    header->addWidget(presetCombo);
    root->addLayout(header);

    // ── Body: canvas + sidebar ──────────────────────────────────────────────
    auto *body = new QHBoxLayout();
    body->setSpacing(12);
    m_canvas = new TransformCanvasWidget();
    m_canvas->setCanvasSize(m_origCanvasW, m_origCanvasH);
    body->addWidget(m_canvas, 1);
    body->addWidget(buildSidebar());
    root->addLayout(body, 1);

    // ── Footer: hint + buttons ──────────────────────────────────────────────
    auto *footer = new QHBoxLayout();
    auto *hint = new QLabel("Drag to move · handles resize · Shift keeps aspect · "
                            "Alt disables snapping · Arrow keys nudge · Double-click fills canvas");
    hint->setProperty("role", "secondary");
    hint->setStyleSheet("font-size: 10px;");
    footer->addWidget(hint);
    footer->addStretch();
    auto *cancelBtn = new QPushButton("Cancel");
    auto *doneBtn = new QPushButton("Done");
    doneBtn->setObjectName("accentButton");
    for (QPushButton *b : { cancelBtn, doneBtn }) {
        b->setAutoDefault(false);
        b->setDefault(false);
        b->setMinimumWidth(90);
    }
    footer->addWidget(cancelBtn);
    footer->addWidget(doneBtn);
    root->addLayout(footer);

    connect(cancelBtn, &QPushButton::clicked, this, &TransformEditorDialog::reject);
    connect(doneBtn, &QPushButton::clicked, this, &TransformEditorDialog::accept);

    connect(m_canvas, &TransformCanvasWidget::clipRectChanged,
            this, &TransformEditorDialog::applyRectLive);
    connect(m_canvas, &TransformCanvasWidget::selectionChanged,
            this, &TransformEditorDialog::onCanvasSelectionChanged);

    connect(m_canvasWSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TransformEditorDialog::onCanvasSizeEdited);
    connect(m_canvasHSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TransformEditorDialog::onCanvasSizeEdited);
    connect(presetCombo, QOverload<int>::of(&QComboBox::activated),
            this, [this, presetCombo](int idx) {
        const QSize s = presetCombo->itemData(idx).toSize();
        if (!s.isValid()) return;
        m_canvasWSpin->setValue(s.width());
        m_canvasHSpin->setValue(s.height());
    });

    populate();
}

QWidget *TransformEditorDialog::buildSidebar() {
    auto *side = new QWidget();
    side->setFixedWidth(256);
    auto *v = new QVBoxLayout(side);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    auto *layersLabel = new QLabel("LAYERS   (top → bottom)");
    layersLabel->setProperty("role", "secondary");
    layersLabel->setStyleSheet("font-size: 10px; font-weight: bold; letter-spacing: 1px;");
    v->addWidget(layersLabel);

    m_layerList = new QListWidget();
    m_layerList->setSelectionMode(QAbstractItemView::SingleSelection);
    v->addWidget(m_layerList, 1);
    connect(m_layerList, &QListWidget::currentRowChanged,
            this, &TransformEditorDialog::onListRowChanged);

    m_placementGroup = new QGroupBox("Placement");
    auto *grid = new QGridLayout(m_placementGroup);
    grid->setContentsMargins(10, 14, 10, 10);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);
    auto makeSpin = [this]() {
        auto *s = new QSpinBox();
        s->setRange(0, 8192);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setAlignment(Qt::AlignCenter);
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &TransformEditorDialog::onPlacementSpinEdited);
        return s;
    };
    auto dimLabel = [](const char *t) {
        auto *l = new QLabel(t);
        l->setProperty("role", "secondary");
        l->setStyleSheet("font-size: 11px;");
        return l;
    };
    m_xSpin = makeSpin(); m_ySpin = makeSpin();
    m_wSpin = makeSpin(); m_hSpin = makeSpin();
    grid->addWidget(dimLabel("X"), 0, 0); grid->addWidget(m_xSpin, 0, 1);
    grid->addWidget(dimLabel("Y"), 0, 2); grid->addWidget(m_ySpin, 0, 3);
    grid->addWidget(dimLabel("W"), 1, 0); grid->addWidget(m_wSpin, 1, 1);
    grid->addWidget(dimLabel("H"), 1, 2); grid->addWidget(m_hSpin, 1, 3);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(4);
    auto *fitBtn = new QPushButton("Fit");
    auto *stretchBtn = new QPushButton("Stretch");
    auto *centerBtn = new QPushButton("Center");
    fitBtn->setToolTip("Fit inside the canvas, keeping the source aspect");
    stretchBtn->setToolTip("Stretch to fill the whole canvas");
    centerBtn->setToolTip("Center on the canvas, keeping the current size");
    for (QPushButton *b : { fitBtn, stretchBtn, centerBtn }) btnRow->addWidget(b);
    grid->addLayout(btnRow, 2, 0, 1, 4);
    v->addWidget(m_placementGroup);

    connect(stretchBtn, &QPushButton::clicked, this, [this]() {
        pushSelectedRect(QRectF(0, 0, 1, 1));
    });
    connect(centerBtn, &QPushButton::clicked, this, [this]() {
        const int sel = m_canvas->selectedIndex();
        if (sel < 0) return;
        const QRectF r = m_canvas->getClips()[sel].rect;
        pushSelectedRect(QRectF((1.0 - r.width()) / 2, (1.0 - r.height()) / 2,
                                r.width(), r.height()));
    });
    connect(fitBtn, &QPushButton::clicked, this, [this]() {
        const int sel = m_canvas->selectedIndex();
        if (sel < 0) return;
        const QPixmap &thumb = m_canvas->getClips()[sel].thumbnail;
        const qreal canvasAR = (qreal)m_canvas->canvasW() / m_canvas->canvasH();
        const qreal srcAR = thumb.isNull() ? canvasAR
                                           : (qreal)thumb.width() / qMax(1, thumb.height());
        qreal w = 1.0, h = 1.0;
        if (srcAR > canvasAR) h = canvasAR / srcAR;
        else                  w = srcAR / canvasAR;
        pushSelectedRect(QRectF((1.0 - w) / 2, (1.0 - h) / 2, w, h));
    });

    return side;
}

void TransformEditorDialog::populate() {
    m_updating = true;

    const auto slotViews = m_editor->layerSlotViews((NodeId)m_contextId);
    QVector<ClipItem> items;
    m_snapshot.clear();
    for (const auto &s : slotViews) {
        ClipItem item;
        item.clipId = s.index;   // slot index doubles as the item id
        item.rect = s.rect;
        item.thumbnail = s.thumb;
        item.name = s.name;
        item.visible = s.visible;
        items.push_back(item);
        m_snapshot.push_back({ s.index, s.rect, s.visible });
    }
    m_clipCount = items.size();
    m_canvas->setClips(items);

    // List rows top→bottom = topmost→bottommost layer (reverse of canvas order).
    m_layerList->clear();
    m_eyeButtons.clear();
    for (int row = 0; row < m_clipCount; ++row) {
        const ClipItem &clip = items[canvasIndexForRow(row)];

        auto *rowWidget = new QWidget();
        auto *h = new QHBoxLayout(rowWidget);
        h->setContentsMargins(6, 2, 6, 2);
        h->setSpacing(8);

        auto *eye = new QToolButton();
        eye->setAutoRaise(true);
        eye->setCheckable(true);
        eye->setChecked(clip.visible);
        eye->setFixedSize(22, 22);
        eye->setToolTip("Show / hide this layer");
        eye->setIcon(Icons::icon(clip.visible ? Icons::Names::Visibility : Icons::Names::VisibilityOff,
                                           16, clip.visible ? Theme::instance().tokens().text
                                                            : Theme::instance().tokens().textDisabled));
        m_eyeButtons.push_back(eye);
        connect(eye, &QToolButton::toggled, this, [this, eye, row](bool on) {
            eye->setIcon(Icons::icon(on ? Icons::Names::Visibility : Icons::Names::VisibilityOff,
                                               16, on ? Theme::instance().tokens().text
                                                      : Theme::instance().tokens().textDisabled));
            const int canvasIdx = canvasIndexForRow(row);
            m_canvas->setClipVisible(canvasIdx, on);
            m_editor->setLayerSlotVisible((NodeId)m_contextId,
                                          m_canvas->getClips()[canvasIdx].clipId, on);
        });

        auto *thumbLabel = new QLabel();
        thumbLabel->setFixedSize(38, 22);
        thumbLabel->setAlignment(Qt::AlignCenter);
        if (!clip.thumbnail.isNull())
            thumbLabel->setPixmap(clip.thumbnail.scaled(38, 22, Qt::KeepAspectRatio,
                                                        Qt::SmoothTransformation));

        auto *nameLabel = new QLabel(clip.name);
        nameLabel->setStyleSheet("font-size: 11px;");

        h->addWidget(eye);
        h->addWidget(thumbLabel);
        h->addWidget(nameLabel, 1);

        auto *item = new QListWidgetItem(m_layerList);
        item->setSizeHint(QSize(0, 32));
        m_layerList->setItemWidget(item, rowWidget);
    }

    m_updating = false;

    // Select the topmost layer so the dialog opens ready to edit.
    if (m_clipCount > 0)
        m_layerList->setCurrentRow(0);
    else
        m_placementGroup->setEnabled(false);
}

// ── Live application ────────────────────────────────────────────────────────

void TransformEditorDialog::applyRectLive(int canvasIndex) {
    const auto clips = m_canvas->getClips();
    if (canvasIndex < 0 || canvasIndex >= clips.size()) return;
    const ClipItem &c = clips[canvasIndex];
    m_editor->setLayerSlotRect((NodeId)m_contextId, c.clipId,
                               c.rect.x(), c.rect.y(),
                               c.rect.width(), c.rect.height());
    if (canvasIndex == m_canvas->selectedIndex())
        syncPlacementSpins();
}

void TransformEditorDialog::pushSelectedRect(const QRectF &rect) {
    const int sel = m_canvas->selectedIndex();
    if (sel < 0) return;
    m_canvas->setClipRect(sel, rect);
    applyRectLive(sel);
}

void TransformEditorDialog::syncPlacementSpins() {
    const int sel = m_canvas->selectedIndex();
    if (sel < 0) return;
    const QRectF r = m_canvas->getClips()[sel].rect;
    m_updating = true;
    m_xSpin->setValue(qRound(r.x() * m_canvas->canvasW()));
    m_ySpin->setValue(qRound(r.y() * m_canvas->canvasH()));
    m_wSpin->setValue(qRound(r.width() * m_canvas->canvasW()));
    m_hSpin->setValue(qRound(r.height() * m_canvas->canvasH()));
    m_updating = false;
}

void TransformEditorDialog::onCanvasSelectionChanged(int canvasIndex) {
    if (m_updating) return;
    m_updating = true;
    m_layerList->setCurrentRow(canvasIndex >= 0 ? rowForCanvasIndex(canvasIndex) : -1);
    m_updating = false;
    m_placementGroup->setEnabled(canvasIndex >= 0);
    if (canvasIndex >= 0) syncPlacementSpins();
}

void TransformEditorDialog::onListRowChanged(int row) {
    if (m_updating) return;
    const int canvasIdx = row >= 0 ? canvasIndexForRow(row) : -1;
    m_canvas->setSelectedIndex(canvasIdx);
    m_placementGroup->setEnabled(canvasIdx >= 0);
    if (canvasIdx >= 0) syncPlacementSpins();
}

void TransformEditorDialog::onPlacementSpinEdited() {
    if (m_updating) return;
    const int sel = m_canvas->selectedIndex();
    if (sel < 0) return;
    const qreal cw = m_canvas->canvasW(), ch = m_canvas->canvasH();
    m_canvas->setClipRect(sel, QRectF(m_xSpin->value() / cw, m_ySpin->value() / ch,
                                      m_wSpin->value() / cw, m_hSpin->value() / ch));
    // Push the clamped result but leave the spin being typed in alone.
    const ClipItem &c = m_canvas->getClips()[sel];
    m_editor->setLayerSlotRect((NodeId)m_contextId, c.clipId,
                               c.rect.x(), c.rect.y(),
                               c.rect.width(), c.rect.height());
}

void TransformEditorDialog::onCanvasSizeEdited() {
    if (m_updating) return;
    const int w = m_canvasWSpin->value(), h = m_canvasHSpin->value();
    m_editor->setLayerCanvasSize((NodeId)m_contextId, w, h);
    m_canvas->setCanvasSize(w, h);
    syncPlacementSpins();
}

// ── Revert on cancel ────────────────────────────────────────────────────────

void TransformEditorDialog::revertAll() {
    m_editor->setLayerCanvasSize((NodeId)m_contextId, m_origCanvasW, m_origCanvasH);
    for (const Snapshot &s : m_snapshot) {
        m_editor->setLayerSlotRect((NodeId)m_contextId, s.slotIndex,
                                   s.rect.x(), s.rect.y(),
                                   s.rect.width(), s.rect.height());
        m_editor->setLayerSlotVisible((NodeId)m_contextId, s.slotIndex, s.visible);
    }
}

void TransformEditorDialog::reject() {
    revertAll();
    QDialog::reject();
}

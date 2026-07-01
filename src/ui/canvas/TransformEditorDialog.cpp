#include "ui/canvas/TransformEditorDialog.h"
#include "ui/canvas/TransformCanvasWidget.h"
#include "ui/nodes/ClipNodeEditor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

TransformEditorDialog::TransformEditorDialog(int contextId, ClipNodeEditor *editor, QWidget *parent)
    : QDialog(parent), m_contextId(contextId), m_editor(editor)
{
    setWindowTitle("Layer Layout");
    setMinimumSize(1000, 800);
    resize(1200, 900);
    setModal(true);

    auto *layout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QString("Place inputs on the layer canvas"));
    titleLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(titleLabel);

    m_canvas = new TransformCanvasWidget();
    int cw = 1280, ch = 720;
    m_editor->layerCanvasSize((NodeId)m_contextId, cw, ch);
    m_canvas->setCanvasSize(cw, ch);
    layout->addWidget(m_canvas);

    auto *buttonLayout = new QHBoxLayout();
    auto *applyBtn = new QPushButton("Apply");
    auto *cancelBtn = new QPushButton("Cancel");
    buttonLayout->addStretch();
    buttonLayout->addWidget(applyBtn);
    buttonLayout->addWidget(cancelBtn);
    layout->addLayout(buttonLayout);

    connect(applyBtn, &QPushButton::clicked, this, &TransformEditorDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &TransformEditorDialog::onCancel);

    populateClips();
}

void TransformEditorDialog::populateClips() {
    QVector<ClipItem> items;
    const auto slotViews = m_editor->layerSlotViews((NodeId)m_contextId);
    for (const auto &s : slotViews) {
        ClipItem item;
        item.clipId = s.index;   // slot index doubles as the item id
        item.rect = s.rect;
        item.thumbnail = s.thumb;
        items.push_back(item);
    }
    m_canvas->setClips(items);
}

void TransformEditorDialog::onApply() {
    const auto clips = m_canvas->getClips();
    for (const auto &clip : clips) {
        m_editor->setLayerSlotRect((NodeId)m_contextId, clip.clipId,
                                   clip.rect.x(), clip.rect.y(),
                                   clip.rect.width(), clip.rect.height());
    }
    accept();
}

void TransformEditorDialog::onCancel() {
    reject();
}

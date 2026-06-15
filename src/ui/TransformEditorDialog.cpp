#include "ui/TransformEditorDialog.h"
#include "ui/TransformCanvasWidget.h"
#include "ui/ClipNodeEditor.h"
#include "ui/ClipNodeModel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

TransformEditorDialog::TransformEditorDialog(int contextId, ClipNodeEditor *editor, QWidget *parent)
    : QDialog(parent), m_contextId(contextId), m_editor(editor)
{
    setWindowTitle("Transform Editor");
    setMinimumSize(600, 500);
    setModal(true);

    auto *layout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QString("Edit Transform - Context %1").arg(contextId));
    titleLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(titleLabel);

    m_canvas = new TransformCanvasWidget();
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

    auto clipIds = m_editor->clipsForContext(m_contextId);
    for (int clipId : clipIds) {
        auto *clipNode = m_editor->nodeAt(clipId);
        if (!clipNode) continue;

        float x, y, w, h;
        if (!m_editor->clipTransform(clipId, x, y, w, h)) continue;

        ClipItem item;
        item.clipId = clipId;
        item.rect = QRectF(x, y, w, h);
        item.thumbnail = clipNode->thumbnail();

        items.push_back(item);
    }

    m_canvas->setClips(items);
}

void TransformEditorDialog::onApply() {
    auto clips = m_canvas->getClips();

    for (const auto &clip : clips) {
        m_editor->setClipTransform(clip.clipId, clip.rect.x(), clip.rect.y(),
                                   clip.rect.width(), clip.rect.height());
    }

    accept();
}

void TransformEditorDialog::onCancel() {
    reject();
}

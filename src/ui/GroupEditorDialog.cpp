#include "ui/GroupEditorDialog.h"
#include "ui/ClipNodeEditor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsView>

GroupEditorDialog::GroupEditorDialog(NodeId groupId, ClipNodeEditor *editor, QWidget *parent)
    : QDialog(parent), m_groupId(groupId), m_editor(editor)
{
    const QString name = editor->groupName(groupId);
    setWindowTitle(QString("%1 (%2)").arg(name).arg(groupId));
    setMinimumSize(800, 600);
    resize(900, 700);

    auto *layout = new QVBoxLayout(this);

    auto *title = new QLabel(QString("Edit \"%1\" — use \"Set output\" on a clip to designate the deck output.")
                                 .arg(name));
    title->setWordWrap(true);
    layout->addWidget(title);

    auto *toolbar = new QHBoxLayout();
    auto *addClipBtn = new QPushButton("Add Clip…", this);
    auto *addContextBtn = new QPushButton("Add Transform Context", this);
    auto *addAudioBtn = new QPushButton("Add Master Audio Output", this);
    toolbar->addWidget(addClipBtn);
    toolbar->addWidget(addContextBtn);
    toolbar->addWidget(addAudioBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    if (ClipNodeScene *subScene = editor->subSceneForGroup(groupId)) {
        m_view = editor->makeSubSceneView(subScene, this, groupId);
        layout->addWidget(m_view);

        auto *view = qobject_cast<QGraphicsView *>(m_view);
        connect(addClipBtn, &QPushButton::clicked, this, [editor, groupId, view]() {
            editor->addClipsFromFileDialog(groupId, view, true);
        });
        connect(addContextBtn, &QPushButton::clicked, this, [editor, groupId, view]() {
            editor->addTransformContextToGroup(groupId, view, true);
        });
        connect(addAudioBtn, &QPushButton::clicked, this, [editor, groupId, view]() {
            editor->addMasterAudioOutputToGroup(groupId, view, true);
        });
    }

    layout->setContentsMargins(8, 8, 8, 8);
}

#include "ui/canvas/GroupEditorDialog.h"
#include "ui/nodes/ClipNodeEditor.h"
#include "ui/mainwindow/SourcePrompt.h"
#include "core/sources/NdiSource.h"
#ifdef PRISM_HAVE_WEBRTC
#include "core/sources/WebRtcSource.h"
#endif
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
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
    auto *addElementBtn = new QPushButton("Add Element ▾", this);
    auto *addContextBtn = new QPushButton("Add Transform Context", this);
    auto *addAudioBtn = new QPushButton("Add Master Audio Output", this);
    auto *addMicBtn = new QPushButton("Add Master Audio Input", this);
    toolbar->addWidget(addElementBtn);
    toolbar->addWidget(addContextBtn);
    toolbar->addWidget(addAudioBtn);
    toolbar->addWidget(addMicBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    if (ClipNodeScene *subScene = editor->subSceneForGroup(groupId)) {
        m_view = editor->makeSubSceneView(subScene, this, groupId);
        layout->addWidget(m_view);

        auto *view = ClipNodeEditor::graphicsViewFrom(m_view);
        QMenu *addMenu = new QMenu(addElementBtn);
        SourcePrompt::buildMenu(addMenu,
            [editor, groupId, view]() {
                editor->addClipsFromFileDialog(groupId, view, true);
            },
            [this, editor, groupId, view](SourceDescriptor::Kind kind) {
                SourceDescriptor desc;
                QPixmap thumb;
                if (SourcePrompt::prompt(kind, this, desc, thumb)) {
                    ClipNodeScene *sub = editor->subSceneForGroup(groupId);
                    editor->addSourceNode(desc, thumb, sub, view, true);
                }
            },
            NdiSource::isAvailable(),
#ifdef PRISM_HAVE_WEBRTC
            WebRtcSource::isAvailable());
#else
            false);
#endif
        addElementBtn->setMenu(addMenu);

        connect(addContextBtn, &QPushButton::clicked, this, [editor, groupId, view]() {
            editor->addTransformContextToGroup(groupId, view, true);
        });
        connect(addAudioBtn, &QPushButton::clicked, this, [editor, groupId, view]() {
            editor->addMasterAudioOutputToGroup(groupId, view, true);
        });
        connect(addMicBtn, &QPushButton::clicked, this, [editor, groupId, view]() {
            editor->addMasterAudioInputToGroup(groupId, view, true);
        });
    }

    layout->setContentsMargins(8, 8, 8, 8);
}

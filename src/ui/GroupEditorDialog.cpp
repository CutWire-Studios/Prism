#include "ui/GroupEditorDialog.h"
#include "ui/ClipNodeEditor.h"
#include "ui/SourcePrompt.h"
#include "core/NdiSource.h"
#ifdef SWITCHX_HAVE_WEBRTC
#include "core/WebRtcSource.h"
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
    toolbar->addWidget(addElementBtn);
    toolbar->addWidget(addContextBtn);
    toolbar->addWidget(addAudioBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    if (ClipNodeScene *subScene = editor->subSceneForGroup(groupId)) {
        m_view = editor->makeSubSceneView(subScene, this, groupId);
        layout->addWidget(m_view);

        auto *view = qobject_cast<QGraphicsView *>(m_view);
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
#ifdef SWITCHX_HAVE_WEBRTC
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
    }

    layout->setContentsMargins(8, 8, 8, 8);
}

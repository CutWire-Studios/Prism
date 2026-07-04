#include "ui/canvas/HtmlWorkspaceCanvasWidget.h"
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEngineView>

HtmlWorkspaceCanvasWidget::HtmlWorkspaceCanvasWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(480, 300);

    m_view = new QWebEngineView(this);
    m_view->setContextMenuPolicy(Qt::NoContextMenu);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    m_bridge = new HtmlCanvasBridge(this);
    auto *channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("bridge"), m_bridge);
    m_view->page()->setWebChannel(channel);

    connect(m_bridge, &HtmlCanvasBridge::ready, this, [this]() {
        m_jsReady = true;
        m_view->page()->runJavaScript(
            QStringLiteral("editor.setPresets(%1);")
                .arg(HtmlPresetRegistry::presetsAsJsonString()));
        m_view->page()->runJavaScript(
            QStringLiteral("editor.setWorkspace(%1);").arg(m_workspace.toJsonString()));
        for (const QString &js : std::as_const(m_pendingJs))
            m_view->page()->runJavaScript(js);
        m_pendingJs.clear();
    });

    connect(m_bridge, &HtmlCanvasBridge::workspaceEdited, this, [this](const QString &json) {
        m_workspace = HtmlWorkspace::fromJsonString(json);
        if (m_selectedIdx >= m_workspace.components.size())
            m_selectedIdx = -1;
        emit workspaceChanged(m_workspace);
    });

    connect(m_bridge, &HtmlCanvasBridge::selectionEdited, this, [this](int index) {
        m_selectedIdx = index;
        emit componentSelected(index);
    });

    m_view->setUrl(QUrl(QStringLiteral("qrc:/html/editor/workspace_editor.html")));
}

void HtmlWorkspaceCanvasWidget::runOrQueue(const QString &js) {
    if (m_jsReady)
        m_view->page()->runJavaScript(js);
    else
        m_pendingJs.append(js);
}

void HtmlWorkspaceCanvasWidget::setWorkspace(const HtmlWorkspace &workspace) {
    m_workspace = workspace;
    if (m_selectedIdx >= m_workspace.components.size())
        m_selectedIdx = -1;
    runOrQueue(QStringLiteral("editor.setWorkspace(%1);").arg(workspace.toJsonString()));
}

void HtmlWorkspaceCanvasWidget::setSelectedIndex(int idx) {
    m_selectedIdx = idx;
    runOrQueue(QStringLiteral("editor.setSelected(%1);").arg(idx));
}

void HtmlWorkspaceCanvasWidget::setSnapEnabled(bool on) {
    runOrQueue(QStringLiteral("editor.setSnap(%1);").arg(on ? "true" : "false"));
}

void HtmlWorkspaceCanvasWidget::setSafeAreasVisible(bool on) {
    runOrQueue(QStringLiteral("editor.setSafeAreas(%1);").arg(on ? "true" : "false"));
}

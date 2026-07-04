#pragma once

#include <QWidget>
#include <QStringList>
#include "core/sources/HtmlWorkspace.h"

class QWebEngineView;

/// QWebChannel endpoint the workspace editor page talks to. The page calls
/// the slots; the widget listens to the signals.
class HtmlCanvasBridge : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void jsReady() { emit ready(); }
    void workspaceChanged(const QString &json) { emit workspaceEdited(json); }
    void selectionChanged(int index) { emit selectionEdited(index); }

signals:
    void ready();
    void workspaceEdited(const QString &json);
    void selectionEdited(int index);
};

/// WYSIWYG editor canvas for an HtmlWorkspace: hosts a QWebEngineView running
/// the bundled workspace editor page, where each component renders live in its
/// own iframe and can be dragged, resized and snapped in place. Also contains
/// the widget palette (with live thumbnails) for drag-and-drop adding.
class HtmlWorkspaceCanvasWidget : public QWidget {
    Q_OBJECT

public:
    explicit HtmlWorkspaceCanvasWidget(QWidget *parent = nullptr);

    void setWorkspace(const HtmlWorkspace &workspace);
    HtmlWorkspace workspace() const { return m_workspace; }

    void setSelectedIndex(int idx);
    int  selectedIndex() const { return m_selectedIdx; }

    void setSnapEnabled(bool on);
    void setSafeAreasVisible(bool on);

signals:
    void workspaceChanged(const HtmlWorkspace &workspace);
    void componentSelected(int index);

private:
    void runOrQueue(const QString &js);

    QWebEngineView  *m_view   = nullptr;
    HtmlCanvasBridge *m_bridge = nullptr;
    HtmlWorkspace     m_workspace;
    int               m_selectedIdx = -1;
    bool              m_jsReady     = false;
    QStringList       m_pendingJs;
};

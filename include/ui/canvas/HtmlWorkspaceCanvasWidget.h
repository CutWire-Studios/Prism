#pragma once

#include <QWidget>
#include <array>
#include "core/sources/HtmlWorkspace.h"

/// Interactive layout canvas for an HtmlWorkspace: drag/resize the placed widget
/// components (clock, score bars, …) in normalized space; emits the edited workspace.
class HtmlWorkspaceCanvasWidget : public QWidget {
    Q_OBJECT

public:
    explicit HtmlWorkspaceCanvasWidget(QWidget *parent = nullptr);

    void setWorkspace(const HtmlWorkspace &workspace);
    HtmlWorkspace workspace() const { return m_workspace; }

    void setSelectedIndex(int idx);
    int  selectedIndex() const { return m_selectedIdx; }

signals:
    void workspaceChanged(const HtmlWorkspace &workspace);
    void componentSelected(int index);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    enum class DragMode { None, Move, ResizeTL, ResizeTR, ResizeBR, ResizeBL };

    struct HitResult { int index; DragMode mode; };

    HtmlWorkspace m_workspace;
    int           m_selectedIdx = -1;
    DragMode      m_dragMode    = DragMode::None;
    QPointF       m_dragOrigin;
    QRectF        m_dragRect;
    HtmlWorkspaceComponent m_dragItem;

    static constexpr double kHandleSize = 9.0;

    QRectF canvasRect() const;
    QRectF compToWidget(const HtmlWorkspaceComponent &c) const;
    HtmlWorkspaceComponent applyWidgetRect(HtmlWorkspaceComponent base, const QRectF &r) const;
    std::array<QRectF, 4> handles(const HtmlWorkspaceComponent &c) const;
    HitResult hitTest(QPointF pt) const;
    void drawComponent(QPainter &p, const HtmlWorkspaceComponent &c, bool selected) const;
    void emitChanged();
    QString presetLabel(const QString &presetId) const;
};

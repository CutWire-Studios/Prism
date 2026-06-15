#pragma once

#include <QWidget>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QMouseEvent>
#include <QPaintEvent>
#include <array>

class BasePlacementWidget : public QWidget {
    Q_OBJECT
public:
    explicit BasePlacementWidget(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);
    void setPlacement(float x, float y, float w, float h);

    float placementX() const { return m_px; }
    float placementY() const { return m_py; }
    float placementW() const { return m_pw; }
    float placementH() const { return m_ph; }

signals:
    void placementChanged(float x, float y, float w, float h);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    QSize sizeHint() const override { return QSize(480, 270); }

private:
    enum class DragMode { None, Move, ResizeTL, ResizeTR, ResizeBR, ResizeBL };

    QImage  m_frame;
    float   m_px = 0.f, m_py = 0.f, m_pw = 1.f, m_ph = 1.f;
    DragMode m_dragMode = DragMode::None;
    QPointF  m_dragOrigin;
    QRectF   m_dragRect;
    bool     m_dragMoved = false;

    QRectF placementRect() const;
    std::array<QRectF, 4> handles(const QRectF &r) const;
    DragMode hitTest(QPointF pt) const;
    void commitDrag(const QRectF &r);
};

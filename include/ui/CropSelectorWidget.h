#pragma once
#include <QWidget>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <array>

// Interactive crop-region selector widget.
// Shows a scaled video frame and lets the user drag the crop body to move it or
// drag the corner handles to resize it.
// All coordinates exposed/emitted are normalized [0,1] relative to the frame.
class CropSelectorWidget : public QWidget {
    Q_OBJECT
public:
    explicit CropSelectorWidget(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);
    void setCrop(float x, float y, float w, float h);

    float cropX() const { return m_cx; }
    float cropY() const { return m_cy; }
    float cropW() const { return m_cw; }
    float cropH() const { return m_ch; }

signals:
    void cropChanged(float x, float y, float w, float h);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    QSize sizeHint() const override { return QSize(480, 270); }

private:
    enum class DragMode { None, Create, Move, ResizeTL, ResizeTR, ResizeBR, ResizeBL };

    QImage  m_frame;
    float   m_cx = 0.f, m_cy = 0.f, m_cw = 1.f, m_ch = 1.f;
    DragMode m_dragMode = DragMode::None;
    QPointF  m_dragOrigin;
    QRectF   m_dragRect;
    bool     m_dragMoved = false;

    QRectF frameRect() const;
    QRectF  cropInWidget() const;
    std::array<QRectF, 4> handles(const QRectF &crop) const;
    DragMode hitTest(QPointF pt) const;
    void    commitDrag(const QRectF &crop);
};

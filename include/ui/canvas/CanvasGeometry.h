#pragma once

#include <QRectF>
#include <QPointF>
#include <algorithm>
#include <array>

/// Pure geometry helpers shared by the interactive placement canvases
/// (CropSelectorWidget, TransformCanvasWidget, …).
///
/// These widgets all map between two coordinate spaces:
///   * normalized space — rects/points in [0,1] relative to a "content" rect
///     (the letterboxed video frame or logical canvas);
///   * widget space     — pixels inside the QWidget.
///
/// Keeping the conversions here (rather than reimplementing them per widget)
/// means a single, tested definition of how content is letterboxed, how
/// normalized coordinates project onto the widget, and where the resize
/// handles sit. The functions are stateless and side-effect free.
namespace CanvasGeometry {

/// Letterbox a content region of aspect ratio `aspect` (width/height) centered
/// inside a `width` x `height` widget, reserving `margin` pixels on every side.
/// Returns the pixel rect the content should occupy.
inline QRectF letterbox(double aspect, double width, double height, double margin = 0.0) {
    const double aw = width  - 2.0 * margin;
    const double ah = height - 2.0 * margin;
    if (aw <= 0.0 || ah <= 0.0 || aspect <= 0.0)
        return QRectF(margin, margin, std::max(0.0, aw), std::max(0.0, ah));

    const double availAspect = aw / ah;
    double dw, dh;
    if (aspect > availAspect) { dw = aw;            dh = aw / aspect; }
    else                      { dh = ah;            dw = ah * aspect; }
    return QRectF(margin + (aw - dw) / 2.0, margin + (ah - dh) / 2.0, dw, dh);
}

/// Project a normalized rect (coords relative to `content`) into widget space.
inline QRectF mapRect(const QRectF &content, const QRectF &norm) {
    return QRectF(content.left() + norm.left() * content.width(),
                  content.top()  + norm.top()  * content.height(),
                  norm.width()  * content.width(),
                  norm.height() * content.height());
}

/// Inverse of mapRect: express a widget-space rect as normalized coords.
inline QRectF unmapRect(const QRectF &content, const QRectF &widgetRect) {
    return QRectF((widgetRect.left() - content.left()) / content.width(),
                  (widgetRect.top()  - content.top())  / content.height(),
                  widgetRect.width()  / content.width(),
                  widgetRect.height() / content.height());
}

/// Inverse projection for a single point.
inline QPointF unmapPoint(const QRectF &content, const QPointF &widgetPos) {
    return QPointF((widgetPos.x() - content.left()) / content.width(),
                   (widgetPos.y() - content.top())  / content.height());
}

/// The four square corner handles of `r`, side `size`, centered on each corner.
/// Order is top-left, top-right, bottom-right, bottom-left.
inline std::array<QRectF, 4> cornerHandles(const QRectF &r, double size) {
    const double h = size / 2.0;
    return { QRectF(r.left()  - h, r.top()    - h, size, size),
             QRectF(r.right() - h, r.top()    - h, size, size),
             QRectF(r.right() - h, r.bottom() - h, size, size),
             QRectF(r.left()  - h, r.bottom() - h, size, size) };
}

} // namespace CanvasGeometry

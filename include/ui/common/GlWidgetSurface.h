#pragma once

#include <QOpenGLWidget>
#include <QSize>
#include <QtMath>

namespace GlWidgetSurface {

/// Device-pixel draw size for a QOpenGLWidget. resizeGL() can report logical
/// pixels on some Windows/Qt builds while the internal FBO stays HiDPI-sized.
inline QSize drawSize(const QOpenGLWidget *widget, int resizeGlW, int resizeGlH) {
    const qreal dpr = widget->devicePixelRatioF();
    const int deviceW = qMax(1, qRound(widget->width()  * dpr));
    const int deviceH = qMax(1, qRound(widget->height() * dpr));
    const int w = resizeGlW > 0 ? qMax(resizeGlW, deviceW) : deviceW;
    const int h = resizeGlH > 0 ? qMax(resizeGlH, deviceH) : deviceH;
    return QSize(w, h);
}

} // namespace GlWidgetSurface

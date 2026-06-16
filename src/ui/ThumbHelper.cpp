#include "ui/ThumbHelper.h"
#include "core/ShaderSource.h"
#include <QPainter>
#include <QFont>
#include <QWebEngineView>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

QPixmap ThumbHelper::makeIconThumb(const QString &glyph, int w, int h) {
    QPixmap pix(w, h);
    pix.fill(QColor("#1c1d1f"));
    QPainter p(&pix);
    QFont f;
    f.setPixelSize(32);
    p.setFont(f);
    p.setPen(QColor("#888888"));
    p.drawText(pix.rect(), Qt::AlignCenter, glyph);
    return pix;
}

QPixmap ThumbHelper::makeCanvasThumb(const QString &label,
                                     SourceDescriptor::CanvasFill fill,
                                     const QColor &color,
                                     int w, int h) {
    QPixmap pix(w, h);
    if (fill == SourceDescriptor::CanvasFill::Color) {
        pix.fill(color);
        return pix;
    }

    pix.fill(QColor("#1c1d1f"));
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor("#8b93a1"));
    p.setBrush(Qt::NoBrush);
    p.drawRect(8, 8, w - 16, h - 16);
    p.setPen(QColor("#c8ccd4"));
    p.drawText(pix.rect(), Qt::AlignCenter,
               fill == SourceDescriptor::CanvasFill::Transparent ? "TR" : label);
    return pix;
}

QPixmap ThumbHelper::makeShaderThumb(const QString &code, int w, int h) {
    ShaderSource src(code, QSize(w, h));
    if (!src.nextFrame() || !src.isReady())
        return makeIconThumb("≋", w, h);
    const uint8_t *data = src.frameData();
    QImage img(data, w, h, w * 3, QImage::Format_RGB888);
    return QPixmap::fromImage(img.copy());
}

QPixmap ThumbHelper::makeHtmlThumb(const QString &html, const QString &filePath, int w, int h) {
    QWebEngineView view;
    view.resize(1280, 720);
    view.setAttribute(Qt::WA_TranslucentBackground);
    view.page()->setBackgroundColor(Qt::transparent);
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.show();

    QEventLoop loop;
    QObject::connect(&view, &QWebEngineView::loadFinished, &loop, &QEventLoop::quit);
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);

    if (!filePath.isEmpty())
        view.load(QUrl::fromLocalFile(filePath));
    else
        view.setHtml(html, QUrl("qrc:/"));

    loop.exec();

    QPixmap grab = view.grab();
    if (grab.isNull())
        return makeIconThumb("🌐", w, h);
    return grab.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

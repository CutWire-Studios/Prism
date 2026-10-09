#include "ui/common/ThumbHelper.h"
#include "ui/common/Icons.h"
#include "core/sources/ShaderSource.h"
#include "ui/common/Theme.h"
#include <QPainter>
#include <QFont>
#include <QWebEngineView>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

namespace {
QColor placeholderFill() {
    const QColor &bg = Theme::instance().tokens().bgBase;
    return Theme::instance().isDark() ? bg.lighter(115) : bg.darker(104);
}
}

QPixmap ThumbHelper::makeIconThumb(const QString &symbolName, int w, int h) {
    const auto &t = Theme::instance().tokens();
    QPixmap pix(w, h);
    pix.fill(placeholderFill());
    QPainter p(&pix);
    p.setPen(t.textSecondary);
    Icons::drawCentered(p, pix.rect(), symbolName.toUtf8().constData(), 32,
                                  t.textSecondary);
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

    const auto &t = Theme::instance().tokens();
    pix.fill(placeholderFill());
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(t.textSecondary);
    p.setBrush(Qt::NoBrush);
    p.drawRect(8, 8, w - 16, h - 16);
    p.setPen(t.text);
    p.drawText(pix.rect(), Qt::AlignCenter,
               fill == SourceDescriptor::CanvasFill::Transparent ? "TR" : label);
    return pix;
}

QPixmap ThumbHelper::makeShaderThumb(const QString &code, int w, int h) {
    ShaderSource src(code, QSize(w, h));
    if (!src.nextFrame() || !src.isReady())
        return makeIconThumb(Icons::Names::Grain, w, h);
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
        return makeIconThumb(Icons::Names::Language, w, h);
    return grab.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QPixmap ThumbHelper::makeTextThumb(const QString &textTemplate, const QColor &color, int w, int h) {
    QPixmap pix(w, h);
    pix.fill(placeholderFill());
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(color);
    QFont f;
    f.setPixelSize(11);
    p.setFont(f);
    const QString label = textTemplate.isEmpty() ? QStringLiteral("Text") : textTemplate;
    p.drawText(pix.rect().adjusted(6, 4, -6, -4),
               Qt::AlignCenter | Qt::TextWordWrap,
               label.length() > 40 ? label.left(37) + QStringLiteral("…") : label);
    return pix;
}

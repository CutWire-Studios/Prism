#include "ui/QrCodeHelper.h"
#include "qrcodegen.hpp"

namespace QrCodeHelper {

QImage toImage(const QString &text, int scale) {
    const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(
        text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int border = 4;
    const int size   = qr.getSize();
    const int pixels = (size + border * 2) * scale;
    QImage img(pixels, pixels, QImage::Format_RGB32);
    img.fill(Qt::white);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!qr.getModule(x, y)) continue;
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx)
                    img.setPixelColor((x + border) * scale + dx,
                                      (y + border) * scale + dy, Qt::black);
            }
        }
    }
    return img;
}

QPixmap toPixmap(const QString &text, int scale) {
    return QPixmap::fromImage(toImage(text, scale));
}

} // namespace QrCodeHelper

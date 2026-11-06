#pragma once

#include <QImage>
#include <QString>

namespace QrCodeHelper {

QImage toImage(const QString &text, int scale = 6);
/// "data:image/png;base64,..." for QML Image.source.
QString toDataUrl(const QString &text, int scale = 6);

} // namespace QrCodeHelper

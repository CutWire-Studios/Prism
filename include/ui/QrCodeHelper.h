#pragma once

#include <QImage>
#include <QPixmap>
#include <QString>

namespace QrCodeHelper {

QImage toImage(const QString &text, int scale = 6);
QPixmap toPixmap(const QString &text, int scale = 6);

} // namespace QrCodeHelper

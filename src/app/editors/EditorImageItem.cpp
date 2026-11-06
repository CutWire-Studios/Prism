#include "app/editors/EditorImageItem.h"

#include <QPainter>

EditorImageItem::EditorImageItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
}

void EditorImageItem::setImage(const QImage &image)
{
    m_image = image;
    update();
    emit imageChanged();
}

void EditorImageItem::setBoxed(bool boxed)
{
    if (m_boxed == boxed)
        return;
    m_boxed = boxed;
    update();
    emit boxedChanged();
}

void EditorImageItem::paint(QPainter *painter)
{
    if (m_image.isNull())
        return;

    constexpr qreal kInset = 6.0;
    constexpr int kCell = 10;
    const QRectF area = m_boxed ? QRectF(0, 0, width(), height()).adjusted(kInset, kInset, -kInset, -kInset)
                                : QRectF(0, 0, width(), height());
    if (area.width() < 1.0 || area.height() < 1.0)
        return;

    QSizeF fit = QSizeF(m_image.size());
    fit.scale(area.size(), Qt::KeepAspectRatio);
    const QRectF target(area.center() - QPointF(fit.width() / 2, fit.height() / 2), fit);
    if (m_boxed) {
        const QRect cells = target.toAlignedRect();
        for (int y = 0; y < cells.height(); y += kCell)
            for (int x = 0; x < cells.width(); x += kCell)
                painter->fillRect(QRect(cells.x() + x, cells.y() + y, kCell, kCell),
                                  ((x / kCell + y / kCell) % 2) ? QColor(0x1a, 0x1a, 0x1a) : QColor(0x24, 0x24, 0x24));
    }
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(target, m_image);
    if (m_boxed) {
        painter->setPen(QColor(0x3a, 0x3a, 0x3a));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(target.adjusted(0, 0, -1, -1));
    }
}

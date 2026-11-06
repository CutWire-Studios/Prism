#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// Paints a QImage fitted to the item. boxed draws the source preview frame: a transparency
// checkerboard and border, inset from the item edge. Unboxed fills the item, for thumbnails.
class EditorImageItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)
    Q_PROPERTY(bool boxed READ boxed WRITE setBoxed NOTIFY boxedChanged)

public:
    explicit EditorImageItem(QQuickItem *parent = nullptr);

    QImage image() const { return m_image; }
    void setImage(const QImage &image);
    bool boxed() const { return m_boxed; }
    void setBoxed(bool boxed);

    void paint(QPainter *painter) override;

signals:
    void imageChanged();
    void boxedChanged();

private:
    QImage m_image;
    bool m_boxed = false;
};

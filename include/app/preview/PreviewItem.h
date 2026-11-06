#pragma once

#include "core/render/Compositor.h"

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

// Shows one of the compositor's streams. When Qt Quick runs on OpenGL and shares a context
// group with the compositor, its texture is wrapped with fromNative (no copy, no upload).
// Otherwise the compositor's cached QImage is uploaded per frame.
class PreviewItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Compositor *compositor READ compositor WRITE setCompositor NOTIFY compositorChanged)
    Q_PROPERTY(Source source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(FillMode fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)

public:
    enum Source { Program, DeckA, DeckB };
    Q_ENUM(Source)
    enum FillMode { Fit, Fill, Stretch };
    Q_ENUM(FillMode)

    explicit PreviewItem(QQuickItem *parent = nullptr);
    ~PreviewItem() override;

    Compositor *compositor() const { return m_compositor; }
    void setCompositor(Compositor *compositor);
    Source source() const { return m_source; }
    void setSource(Source source);
    FillMode fillMode() const { return m_fillMode; }
    void setFillMode(FillMode mode);

signals:
    void compositorChanged();
    void sourceChanged();
    void fillModeChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void syncViewer();

    QPointer<Compositor> m_compositor;
    Source m_source = Program;
    FillMode m_fillMode = Fit;

    QPointer<Compositor> m_viewerOf;
    int m_viewerSource = -1;

    GLuint m_boundTexture = 0;
    QSize m_boundSize;
    qint64 m_boundImageKey = 0;
};

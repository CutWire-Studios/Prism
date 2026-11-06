#include "preview/PreviewItem.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

PreviewItem::PreviewItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

PreviewItem::~PreviewItem()
{
    if (m_viewerOf && m_viewerSource >= 0)
        m_viewerOf->removeViewer(m_viewerSource);
}

void PreviewItem::setCompositor(Compositor *compositor)
{
    if (m_compositor == compositor)
        return;
    if (m_compositor)
        disconnect(m_compositor, nullptr, this, nullptr);
    m_compositor = compositor;
    if (m_compositor)
        connect(m_compositor, &Compositor::frameReady, this, [this] { update(); });
    syncViewer();
    update();
    emit compositorChanged();
}

void PreviewItem::setSource(Source source)
{
    if (m_source == source)
        return;
    m_source = source;
    syncViewer();
    update();
    emit sourceChanged();
}

void PreviewItem::setFillMode(FillMode mode)
{
    if (m_fillMode == mode)
        return;
    m_fillMode = mode;
    update();
    emit fillModeChanged();
}

void PreviewItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange)
        syncViewer();
}

void PreviewItem::syncViewer()
{
    const int wanted = (m_compositor && window() && isVisible()) ? int(m_source) : -1;
    if (m_viewerOf == m_compositor && m_viewerSource == wanted)
        return;
    if (m_viewerOf && m_viewerSource >= 0)
        m_viewerOf->removeViewer(m_viewerSource);
    m_viewerOf = m_compositor;
    m_viewerSource = wanted;
    if (m_viewerOf && m_viewerSource >= 0)
        m_viewerOf->addViewer(m_viewerSource);
}

QSGNode *PreviewItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);

    auto reset = [&]() -> QSGNode * {
        delete node;
        m_boundTexture = 0;
        m_boundSize = {};
        m_boundImageKey = 0;
        return nullptr;
    };

    if (!m_compositor || !window())
        return reset();

    const Deck deck = m_source == Live ? Deck::Live : Deck::Staging;
    const bool native = window()->rendererInterface()->graphicsApi() == QSGRendererInterface::OpenGL
                        && m_compositor->texturesShareable();

    GLuint textureId = 0;
    QImage image;
    QSize drawSize;
    if (native) {
        textureId = m_source == Program ? m_compositor->programTexture() : m_compositor->deckTexture(deck);
        drawSize = m_compositor->programTextureSize();
        if (!textureId)
            return reset();
    } else {
        image = m_source == Program ? m_compositor->programFrame() : m_compositor->deckProgramFrame(deck);
        if (image.isNull())
            return reset();
        drawSize = image.size();
    }

    if (!node)
        node = new QSGSimpleTextureNode();

    const bool needNewWrapper = !node->texture()
        || (native ? (m_boundTexture != textureId || m_boundSize != drawSize)
                   : (m_boundImageKey != image.cacheKey()));

    if (needNewWrapper) {
        QSGTexture *texture = nullptr;
        if (native) {
            // The compositor owns the GL object; the wrapper only borrows it.
            texture = QNativeInterface::QSGOpenGLTexture::fromNative(
                textureId, window(), drawSize, QQuickWindow::TextureHasAlphaChannel);
        } else {
            QImage upload = image;
            if (upload.format() != QImage::Format_ARGB32_Premultiplied)
                upload = upload.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            texture = window()->createTextureFromImage(upload);
        }
        if (!texture) {
            qWarning("PreviewItem: failed to create scene-graph texture");
            return reset();
        }
        node->setTexture(texture);
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        m_boundTexture = native ? textureId : 0;
        m_boundSize = drawSize;
        m_boundImageKey = native ? 0 : image.cacheKey();
    }

    // The program FBO has row 0 at the image top; deck FBOs are GL-upright (row 0 at the bottom).
    node->setTextureCoordinatesTransform(native && m_source != Program
                                             ? QSGSimpleTextureNode::MirrorVertically
                                             : QSGSimpleTextureNode::NoTransform);

    const QRectF bounds = boundingRect();
    node->setSourceRect(QRectF());
    if (drawSize.isEmpty() || bounds.isEmpty() || m_fillMode == Stretch) {
        node->setRect(bounds);
        return node;
    }

    const qreal scale = m_fillMode == Fit
        ? qMin(bounds.width() / drawSize.width(), bounds.height() / drawSize.height())
        : qMax(bounds.width() / drawSize.width(), bounds.height() / drawSize.height());
    if (m_fillMode == Fit) {
        const qreal w = drawSize.width() * scale;
        const qreal h = drawSize.height() * scale;
        node->setRect(QRectF(bounds.x() + (bounds.width() - w) / 2.0,
                             bounds.y() + (bounds.height() - h) / 2.0, w, h));
    } else {
        const qreal visW = bounds.width() / scale;
        const qreal visH = bounds.height() / scale;
        node->setRect(bounds);
        node->setSourceRect(QRectF((drawSize.width() - visW) / 2.0,
                                   (drawSize.height() - visH) / 2.0, visW, visH));
    }
    return node;
}

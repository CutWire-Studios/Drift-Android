#include "PreviewItem.h"

#include "playback/PlaybackEngine.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

PreviewItem::PreviewItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void PreviewItem::setPlayback(PlaybackEngine *engine)
{
    if (m_playback == engine)
        return;

    if (m_playback)
        disconnect(m_playback, nullptr, this, nullptr);

    m_playback = engine;
    if (m_playback) {
        connect(m_playback, &PlaybackEngine::currentFrameChanged, this, &PreviewItem::pullFrame);
        pullFrame();
    }
    emit playbackChanged();
}

void PreviewItem::pullFrame()
{
    if (!m_playback)
        return;

    // Pull in C++: QML cannot reliably assign QImage between properties, which left
    // the Android preview permanently blank even when the compositor had frames.
    setTextureSize(m_playback->previewTextureSize());
    setTextureId(m_playback->previewTextureId());
    setImage(m_playback->previewImage());
}

void PreviewItem::setTextureId(int id)
{
    if (m_textureId == id)
        return;
    m_textureId = id;
    emit frameChanged();
    update();
}

void PreviewItem::setTextureSize(const QSize &size)
{
    if (m_textureSize == size)
        return;
    m_textureSize = size;
    emit frameChanged();
    update();
}

void PreviewItem::setImage(const QImage &image)
{
    // cacheKey changes whenever pixel data is replaced, even for same size.
    if (m_image.cacheKey() == image.cacheKey())
        return;
    m_image = image;
    if (!m_image.isNull() && m_textureSize.isEmpty())
        m_textureSize = m_image.size();
    emit frameChanged();
    update();
}

QSGNode *PreviewItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);

    const bool hasImage = !m_image.isNull();
    const bool hasNative = m_textureId != 0 && !m_textureSize.isEmpty();
    if ((!hasImage && !hasNative) || !window()) {
        delete node;
        m_boundTextureId = 0;
        m_boundTextureSize = {};
        m_boundImageCacheKey = 0;
        return nullptr;
    }

    if (!node)
        node = new QSGSimpleTextureNode();

    const QSize drawSize = hasImage ? m_image.size()
                                    : m_textureSize;
    const bool needNewWrapper = !node->texture()
        || (hasImage && m_boundImageCacheKey != m_image.cacheKey())
        || (!hasImage && (m_boundTextureId != m_textureId || m_boundTextureSize != m_textureSize));

    if (needNewWrapper) {
        QSGTexture *texture = nullptr;
        if (hasImage) {
            // Qt Quick's GLES/ANGLE path needs premultiplied ARGB32. RGBA8888 (and
            // TextureCanUseAtlas) silently produced a blank node on Android even when
            // the compositor handed over a valid QImage.
            QImage upload = m_image;
            if (upload.format() != QImage::Format_ARGB32_Premultiplied)
                upload = upload.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            texture = window()->createTextureFromImage(upload);
        } else {
            // Wraps the compositor's framebuffer texture — no copy, no upload. The GL
            // object stays owned by the engine's presentation ring, so the scene graph
            // must not take ownership of it; setOwnsTexture only frees this wrapper.
            texture = QNativeInterface::QSGOpenGLTexture::fromNative(
                static_cast<GLuint>(m_textureId), window(), m_textureSize);
        }
        if (!texture) {
            qWarning("PreviewItem: failed to create scene-graph texture (image=%dx%d id=%d)",
                     drawSize.width(), drawSize.height(), m_textureId);
            delete node;
            m_boundTextureId = 0;
            m_boundTextureSize = {};
            m_boundImageCacheKey = 0;
            return nullptr;
        }
        node->setTexture(texture);
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        node->setTextureCoordinatesTransform(QSGSimpleTextureNode::NoTransform);
        m_boundTextureId = hasImage ? 0 : m_textureId;
        m_boundTextureSize = drawSize;
        m_boundImageCacheKey = hasImage ? m_image.cacheKey() : 0;
    }

    // No flip: the compositor promotes every source into its framebuffer such
    // that row 0 holds the image's top row (which is why toImage(false) comes out
    // upright), and the scene graph likewise samples v=0 at the top.

    const QRectF bounds = boundingRect();
    if (drawSize.isEmpty() || bounds.isEmpty()) {
        node->setRect(bounds);
        return node;
    }
    const qreal scale = qMin(bounds.width() / drawSize.width(),
                             bounds.height() / drawSize.height());
    const qreal drawW = drawSize.width() * scale;
    const qreal drawH = drawSize.height() * scale;
    const qreal x = bounds.x() + (bounds.width() - drawW) / 2.0;
    const qreal y = bounds.y() + (bounds.height() - drawH) / 2.0;
    node->setRect(QRectF(x, y, drawW, drawH));

    return node;
}

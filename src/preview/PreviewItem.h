#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QSize>

class PlaybackEngine;

// Displays the compositor's output. The frame is normally already a GL texture in
// the shared context, so it is wrapped with fromNative — no copy, no upload. Only
// when the driver refused to share contexts does the compositor publish a QImage
// instead, which is uploaded here per frame.
//
// Frames are pulled from PlaybackEngine in C++ — QML cannot reliably assign QImage
// between properties, which left the Android preview permanently blank.
class PreviewItem : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(PlaybackEngine *playback READ playback WRITE setPlayback NOTIFY playbackChanged)
    Q_PROPERTY(int textureId READ textureId WRITE setTextureId NOTIFY frameChanged)
    Q_PROPERTY(QSize textureSize READ textureSize WRITE setTextureSize NOTIFY frameChanged)
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY frameChanged)

public:
    explicit PreviewItem(QQuickItem *parent = nullptr);

    PlaybackEngine *playback() const { return m_playback; }
    void setPlayback(PlaybackEngine *engine);

    int textureId() const { return m_textureId; }
    void setTextureId(int id);

    QSize textureSize() const { return m_textureSize; }
    void setTextureSize(const QSize &size);

    QImage image() const { return m_image; }
    void setImage(const QImage &image);

signals:
    void frameChanged();
    void playbackChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    void pullFrame();

    QPointer<PlaybackEngine> m_playback;
    int m_textureId = 0;
    QSize m_textureSize;
    QImage m_image;
    // Last native id / image identity wrapped into the scene-graph node.
    int m_boundTextureId = 0;
    QSize m_boundTextureSize;
    qint64 m_boundImageCacheKey = 0;
};

#pragma once

#include <QQuickImageProvider>

// Serves the segmentation window's reference frame and preview mask from SegmentImageStore, as
// image://segment/frame and image://segment/mask. Both change on every prompt edit and never hit
// disk, so there is nothing for DriftImageProvider's path-based lookup to load.
class SegmentImageProvider : public QQuickImageProvider
{
public:
    SegmentImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

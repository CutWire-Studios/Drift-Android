#pragma once

#include <QQuickImageProvider>

// Serves the speed-curve window's decoded clip frame from ClipPreviewImageStore, as
// image://clippreview/frame. The pixels change on every pump tick and never hit disk, so there
// is nothing for DriftImageProvider's path-based lookup to load.
class ClipPreviewImageProvider : public QQuickImageProvider
{
public:
    ClipPreviewImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

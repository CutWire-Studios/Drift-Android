#pragma once

#include <QQuickImageProvider>

// Renders a style pack's preview card through the real text rasterizer, so the picker shows exactly
// what the compositor will draw — accents included. Uses each pack's sampleText. Requested as
// image://textstyle/<presetId>.
class TextStylePreviewImageProvider : public QQuickImageProvider
{
public:
    TextStylePreviewImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

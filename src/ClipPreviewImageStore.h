#pragma once

#include <QImage>

// The speed-curve window's decoded frame, held in memory between the pump thread that decodes
// it and the QML image-loading thread that serves it.
//
// Split out from ClipPreviewImageProvider for the same reason SegmentImageStore is: the
// model-layer tests link Gui and Concurrent but not Quick.
namespace ClipPreviewImageStore {

void setFrame(const QImage &frame);
void clear();

QImage frame();

} // namespace ClipPreviewImageStore

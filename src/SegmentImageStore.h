#pragma once

#include <QImage>

// The segmentation window's reference frame and preview mask, held in memory between the
// controller that produces them and the QML image provider that serves them.
//
// Split out from SegmentImageProvider so AppController does not have to pull in QtQuick: the
// model-layer tests link Gui and Concurrent but not Quick.
namespace SegmentImageStore {

// Written from the GUI thread, read on QML's image-loading thread.
void setFrame(const QImage &frame);
void setMask(const QImage &mask);
void clear();

QImage frame();
QImage mask();

} // namespace SegmentImageStore

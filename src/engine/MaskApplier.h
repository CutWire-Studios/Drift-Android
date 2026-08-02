#pragma once

#include "core/Mask.h"

#include <QImage>

namespace drift {

// Grayscale8 coverage map for the mask, white where the frame shows through.
// The GPU compositor uploads this as a texture and multiplies alpha in a shader,
// so it is built once per (mask, size) rather than per frame.
QImage maskAlphaMap(const Mask &mask, int canvasWidth, int canvasHeight);

QImage applyMask(const QImage &frame, const Mask &mask, int canvasWidth, int canvasHeight);

} // namespace drift

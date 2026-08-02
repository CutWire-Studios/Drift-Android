#pragma once

#include "core/Time.h"

#include <QString>

#include <functional>

namespace drift {

// Writes [coverInUs, coverOutUs] of sourcePath, reversed, to outPath.
//
// A reversed clip asks the decoder for an ever-earlier timestamp, which costs a keyframe seek and
// a GOP re-decode per displayed frame. Rendering the range once lets playback read the result
// forwards instead. Each output frame keeps the exact mirror of its source timestamp
// (pts = coverOut - srcPts), so the mapping stays exact on variable-frame-rate sources and no
// frame rate has to be assumed.
//
// Opens its own AVFormatContext rather than going through ClipReaderPool, so preview playback
// keeps working (slowly, on the live path) while a render is in flight.
//
// Memory is bounded regardless of clip length: frames are gathered a GOP at a time under a byte
// budget, never a whole clip. onProgress is called with 0..1 and returns false to cancel; on
// cancel or failure no file is left behind.
bool renderReversed(const QString &sourcePath, TimeUs coverInUs, TimeUs coverOutUs,
                    const QString &outPath, QString *errorOut,
                    const std::function<bool(double)> &onProgress);

// Peak decoded-frame memory held while reversing a batch.
constexpr qint64 kReverseBatchByteBudget = 256LL * 1024 * 1024;

} // namespace drift

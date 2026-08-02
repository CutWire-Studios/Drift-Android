#pragma once

#include "core/Effect.h"
#include "core/Time.h"
#include "engine/FaceLandmarker.h"

#include <QImage>
#include <QList>

// Applies per-clip libavfilter effects to a single RGBA frame.
class EffectProcessor
{
public:
    // This frame's face anchors, one entry per tracked slot, as produced by
    // FaceTrack::sampleAll(). Effects declaring "requires": "face" pick their slot from it.
    // Empty — or a slot with no face on this frame — leaves u_faceValid at 0, which every face
    // shader treats as pass-through.
    static QImage applyEffects(const QImage &input, const QList<drift::Effect> &effects,
                               drift::TimeUs timeUs = 0,
                               const QList<drift::FaceAnchors> &faceSlots = {});
};

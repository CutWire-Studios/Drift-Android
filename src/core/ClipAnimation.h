#pragma once

#include "FadeShape.h"
#include "Time.h"

#include <QJsonObject>
#include <QString>

namespace drift {

// CapCut-style body intro/outro for a whole clip (video/image/shape/text).
// Fade is one kind among slide/zoom/pop — same duration + style (curve) as the rest.
enum class ClipAnimKind {
    None,
    Fade,
    SlideUp,
    SlideDown,
    SlideLeft,
    SlideRight,
    ZoomIn,
    ZoomOut,
    Pop,
    SpinCW,
    SpinCCW,
    Bounce
};

// Legacy ease names kept for project JSON compatibility. New UI uses FadeCurve.
enum class ClipAnimEase { Linear, EaseOut, EaseInOut, Back };

struct ClipAnimation
{
    ClipAnimKind kind = ClipAnimKind::None;
    TimeUs durationUs = 500000; // 0.5s
    ClipAnimEase ease = ClipAnimEase::EaseOut; // legacy
    FadeCurve curve = FadeCurve::Smooth;
    FadeShape shape; // when curve == Custom
};

struct ClipAnimSample
{
    double opacity = 1.0;
    double dx = 0.0;
    double dy = 0.0;
    double scale = 1.0;
    double rotationDeg = 0.0;
};

QString clipAnimKindToString(ClipAnimKind kind);
ClipAnimKind clipAnimKindFromString(const QString &kind);

QString clipAnimEaseToString(ClipAnimEase ease);
ClipAnimEase clipAnimEaseFromString(const QString &ease);

FadeCurve clipAnimEaseToCurve(ClipAnimEase ease);
ClipAnimEase clipAnimCurveToEase(FadeCurve curve);

QJsonObject clipAnimationToJson(const ClipAnimation &anim);
ClipAnimation clipAnimationFromJson(const QJsonObject &object);

// Settled sample for the clip at `timelineUs`. `layoutW`/`layoutH` are canvas pixels
// (already render-scaled) so slide travel matches what the user sees.
// Fade kind is a no-op here — opacity comes from Clip::fadeMultiplier.
ClipAnimSample evaluateClipAnimation(TimeUs timelineStart, TimeUs timelineDuration,
                                     const ClipAnimation &animIn, const ClipAnimation &animOut,
                                     TimeUs timelineUs, double layoutW, double layoutH);

} // namespace drift

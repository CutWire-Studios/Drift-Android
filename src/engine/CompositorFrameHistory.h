#pragma once

#include <QImage>
#include <QList>
#include <QString>

// Deterministic frame-history blending for compositor-only temporal effects.
namespace CompositorFrameHistory {

enum class EchoBlendMode
{
    Normal,
    Add,
    Screen,
};

EchoBlendMode parseEchoBlendMode(const QString &value);

// framesNewestFirst[0] is the current frame; [1] is one project-frame earlier, etc.
// decay in [0, 1] weights older samples by decay^age (current is always full weight).
QImage applyTimeEcho(const QList<QImage> &framesNewestFirst, double decay, EchoBlendMode blendMode);

} // namespace CompositorFrameHistory

#include "CompositorFrameHistory.h"

#include "GpuEffectExecutor.h"

#include <QPainter>
#include <QtMath>

namespace CompositorFrameHistory {

namespace {

QPainter::CompositionMode toQtComposition(EchoBlendMode mode)
{
    switch (mode) {
    case EchoBlendMode::Add:
        return QPainter::CompositionMode_Plus;
    case EchoBlendMode::Screen:
        return QPainter::CompositionMode_Screen;
    case EchoBlendMode::Normal:
        break;
    }
    return QPainter::CompositionMode_SourceOver;
}

void compositeEchoLayer(QImage &base, const QImage &overlay, double opacity, EchoBlendMode mode)
{
    if (overlay.isNull() || opacity <= 0.0)
        return;

    if (base.isNull()) {
        base = overlay.convertToFormat(QImage::Format_RGBA8888);
        return;
    }

    if (base.size() != overlay.size())
        return;

    QPainter painter(&base);
    painter.setCompositionMode(toQtComposition(mode));
    painter.setOpacity(qBound(0.0, opacity, 1.0));
    painter.drawImage(0, 0, overlay);
}

int blendModeIndex(EchoBlendMode mode)
{
    switch (mode) {
    case EchoBlendMode::Add:
        return 1;
    case EchoBlendMode::Screen:
        return 2;
    case EchoBlendMode::Normal:
        break;
    }
    return 0;
}

} // namespace

EchoBlendMode parseEchoBlendMode(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("add"))
        return EchoBlendMode::Add;
    if (normalized == QStringLiteral("screen"))
        return EchoBlendMode::Screen;
    return EchoBlendMode::Normal;
}

QImage applyTimeEcho(const QList<QImage> &framesNewestFirst, double decay, EchoBlendMode blendMode)
{
    if (framesNewestFirst.isEmpty())
        return {};

    if (GpuEffectExecutor::instance().isAvailable()) {
        const QImage gpu =
            GpuEffectExecutor::instance().blendTimeEcho(framesNewestFirst, decay,
                                                        blendModeIndex(blendMode));
        if (!gpu.isNull())
            return gpu;
    }

    const double clampedDecay = qBound(0.0, decay, 1.0);
    QImage result;
    for (int age = framesNewestFirst.size() - 1; age >= 0; --age) {
        const QImage layer = framesNewestFirst.at(age).convertToFormat(QImage::Format_RGBA8888);
        if (layer.isNull())
            continue;

        const double opacity = age == 0 ? 1.0 : qPow(clampedDecay, static_cast<double>(age));
        compositeEchoLayer(result, layer, opacity, blendMode);
    }

    return result;
}

} // namespace CompositorFrameHistory

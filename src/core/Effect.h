#pragma once

#include "Keyframe.h"
#include "Time.h"

#include <QMap>
#include <QString>
#include <QVariant>

namespace drift {

// Per-clip libavfilter effect with named parameters.
struct Effect
{
    QString name;
    QMap<QString, QVariant> parameters;

    // Params with a non-empty track are animated: the track wins over `parameters` at
    // render time. `parameters` still holds the last static value, so clearing a track
    // returns the param to a constant rather than to the catalog default.
    QMap<QString, KeyframeTrack<double>> paramKeyframes;

    // Id into the effect preset catalog (src/engine/EffectCatalog.h) this effect was
    // created from; UI-only bookkeeping so the inspector can show the right
    // label/sliders. Rendering uses catalog metadata in the engine layer.
    QString catalogId;

    QString filterGraphString() const;

    // parameters[key], overridden by paramKeyframes[key] evaluated at clipTimeUs.
    QVariant valueAt(const QString &key, TimeUs clipTimeUs) const;

    // A copy with every animated param baked down to its value at clipTimeUs.
    Effect resolvedAt(TimeUs clipTimeUs) const;
};

} // namespace drift

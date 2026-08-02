#pragma once

#include <QList>
#include <QString>

namespace drift {

// User-adjustable parameter metadata for an effect preset (GUI-free).
struct EffectParamSpec
{
    QString key;
    QString label;
    double min = 0.0;
    double max = 1.0;
    double defaultValue = 0.0;
    bool isBoolean = false;
};

// Stable catalog entry describing an effect preset without FFmpeg details.
struct EffectPresetMeta
{
    QString id;           // e.g. "rgb_split", "stylize.bloom"
    QString displayName;  // e.g. "RGB Split"
    QString category;     // stable slug, e.g. "glitch", "retro", "dreamy", "impact"
    QList<EffectParamSpec> parameters;
    bool compositorOnly = false; // true when not expressible via libavfilter alone
};

} // namespace drift

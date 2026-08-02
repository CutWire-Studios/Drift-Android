#pragma once

#include <QVector>

// Pitch-preserving tempo change via libavfilter atempo (chains for values outside 0.5–2.0).
class AudioAtempo
{
public:
    // interleavedStereo has sampleCount frames (sampleCount * 2 floats).
    // Returns stretched/compressed audio with outSampleCount frames.
    static QVector<float> apply(const float *interleavedStereo, int sampleCount, int sampleRate, double tempo,
                                int outSampleCount);
};

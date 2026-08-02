#pragma once

#include <QString>
#include <QVariantList>
#include <QVector>

class MediaWaveform
{
public:
    // Max-abs amplitude peaks for the whole file at a fixed rate, plus the duration they
    // span so callers can map a source second back to an index. Values are raw [0, 1];
    // the display floor is applied by the caller.
    struct Dense
    {
        QVector<float> peaks;
        double durationSeconds = 0.0;
    };

    // Resolution is duration-proportional so a multi-hour file does not collapse into the
    // same handful of buckets as a short one. `maxPeaks` bounds memory (1M floats = 4 MB).
    // Decodes the whole file, so cost scales with duration — prefer peaksForRange() for
    // anything that only needs part of a long source.
    static Dense densePeaks(const QString &sourcePath, int peaksPerSecond = 100,
                            int maxPeaks = 1 << 20);

    // Peaks for [startSeconds, endSeconds) only. Seeks first, so the cost is proportional to
    // the span asked for rather than to the file length. Returns exactly
    // ceil((end - start) * peaksPerSecond) values, or empty if nothing decoded (past the end
    // of the media, no audio stream, unreadable file).
    static QVector<float> peaksForRange(const QString &sourcePath, double startSeconds,
                                        double endSeconds, int peaksPerSecond);

    // Voice-emphasized peaks from already-decoded interleaved-stereo float PCM.
    // Collapses to mono, band-passes to the speech range (300 Hz - 3 kHz, 4th-order at both
    // ends so music outside the band is genuinely rejected rather than just tilted), then
    // buckets max-abs amplitude into `buckets` normalized peaks in [0.05, 1.0].
    static QVariantList voicePeaksFromPcm(const float *interleavedStereo, int frameCount,
                                          int sampleRate, int buckets);
};

#pragma once

#include <QList>

struct AudioOnset
{
    double seconds = 0.0;  // absolute timeline seconds
    float strength = 0.0f; // 0..1, relative to the strongest onset in the range
};

struct AudioBeatAnalysis
{
    QList<AudioOnset> onsets; // detected transients
    QList<double> beats;      // regular grid derived from bpm + phase; empty when bpm == 0
    int beatsPerBar = 4;
    int firstDownbeat = 0; // index into `beats` of the first bar line
    double bpm = 0.0;      // 0 when the tempo estimate is not trustworthy
    double confidence = 0.0;
};

// Spectral-flux onset detection plus autocorrelation tempo estimation, built on
// FFmpeg's av_tx (same FFT primitive as WhisperTranscriber / DeepFilterDenoiser)
// so this costs no extra dependency.
class AudioOnsets
{
public:
    // Mono PCM at `sampleRate`. Returned times are offset by `startSeconds` so they
    // land in absolute timeline space.
    static AudioBeatAnalysis analyze(const float *mono, int frameCount, int sampleRate,
                                     double startSeconds);
};

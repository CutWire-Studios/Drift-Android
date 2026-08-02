#pragma once

#include "core/Project.h"
#include "core/Time.h"
#include "engine/audio/AudioEffectRack.h"

#include <QHash>
#include <QMutex>
#include <QVector>
#include <memory>

// Mixes active audio clips into interleaved stereo float PCM.
class AudioMixer
{
public:
    void setProject(const drift::Project *project);
    void resetEffectRacks();

    void mix(drift::TimeUs timelineStartUs, int sampleCount, int sampleRate, float *interleavedStereoOut) const;

    // One clip's audio in timeline space, with its source read, reverse and speed (constant or
    // curved) applied exactly as playback does. Exposed so the speed-curve editor's preview
    // player auditions the very same retiming the timeline will produce, rather than growing a
    // second implementation of it. Timeline positions outside the clip come back as silence.
    // Returns interleaved stereo of exactly `outFrames` frames.
    static QVector<float> readClipAudio(const drift::Clip &clip, drift::TimeUs winStartUs, int outFrames,
                                        int sampleRate);

private:
    const drift::Project *m_project = nullptr;
    // mix() runs on the audio thread; resetEffectRacks() is called from the GUI thread on seek,
    // play and pause. The mutex covers the hash itself — callers take a shared_ptr copy out of it
    // and work on the rack with the lock released.
    mutable QMutex m_effectRackMutex;
    mutable QHash<QString, std::shared_ptr<drift::AudioEffectRack>> m_effectRacks;
};

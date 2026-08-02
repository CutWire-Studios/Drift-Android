#pragma once

#include "core/Project.h"
#include "core/Time.h"
#include "engine/audio/AudioEffectRack.h"
#include "engine/audio/ClipAudioRetimer.h"

#include <QHash>
#include <QMutex>
#include <QVector>
#include <memory>

// Everything the mixer carries between blocks for one clip. Both halves are streaming DSP whose
// state only means anything while playback runs forward, and both are invalidated at the same
// moments, so they live and die together in one entry.
struct ClipAudioState
{
    drift::AudioEffectRack rack;
    drift::ClipAudioRetimer retimer;
};

// Mixes active audio clips into interleaved stereo float PCM.
class AudioMixer
{
public:
    void setProject(const drift::Project *project);
    void resetClipAudioState();

    void mix(drift::TimeUs timelineStartUs, int sampleCount, int sampleRate, float *interleavedStereoOut) const;

    // One clip's audio in timeline space, with its source read, reverse and speed (constant or
    // curved) applied exactly as playback does. Exposed so the speed-curve editor's preview
    // player auditions the very same retiming the timeline will produce, rather than growing a
    // second implementation of it. Timeline positions outside the clip come back as silence.
    // Returns interleaved stereo of exactly `outFrames` frames.
    //
    // `retimer` carries the stretcher state across blocks. It must be the same one for every block
    // of a given clip, and callers must not share one between clips.
    static QVector<float> readClipAudio(const drift::Clip &clip, drift::TimeUs winStartUs, int outFrames,
                                        int sampleRate, drift::ClipAudioRetimer *retimer);

private:
    const drift::Project *m_project = nullptr;
    // mix() runs on the audio thread; resetClipAudioState() is called from the GUI thread on seek,
    // play and pause. The mutex covers the hash itself — callers take a shared_ptr copy out of it
    // and work on the state with the lock released.
    mutable QMutex m_clipAudioMutex;
    mutable QHash<QString, std::shared_ptr<ClipAudioState>> m_clipAudio;
};

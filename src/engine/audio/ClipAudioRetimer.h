#pragma once

#include "core/Time.h"

#include <QtGlobal>

#include <functional>
#include <memory>

namespace drift {

// Everything the retimer needs to know about one block of one clip. Passed by value every block so
// the retimer never holds a reference into the project, which the GUI thread mutates underneath the
// mixing thread.
struct ClipAudioBlock
{
    // Hash of every clip property that changes the source<->timeline mapping. A change restarts the
    // stream: the alternative is a stretcher still reading from a mapping that no longer exists,
    // which is what dragging the speed slider mid-playback would otherwise produce.
    quint64 identity = 0;
    int sampleRate = 0;
    // Timeline position of the first frame this block emits, and the source position it maps to.
    // The source position is only consulted when the stream restarts; while it runs the retimer
    // walks the source itself.
    drift::TimeUs timelineStartUs = 0;
    drift::TimeUs sourceStartUs = 0;
    double tempo = 1.0;
    bool reverse = false;
};

// Pitch-preserving retiming of one clip's audio, as a continuous stream.
//
// The stretcher's output is only 1/tempo of its input *on average* — it emits in bursts a few audio
// blocks long — so a stateless per-block stretch cannot work: it either starves (dropouts) or grows
// its backlog without bound. Both ends are therefore buffered, and the retimer owns the source read
// cursor rather than deriving it from the timeline each block. That closes the loop: it pulls
// exactly as much source as it needs to keep one block ahead, so input and output rates cannot
// drift apart.
//
// Source reads come in through a callback so this stays in the DSP library, with no ffmpeg and no
// project types behind the header.
//
// Not thread-safe. AudioMixer owns one per clip and touches it only from the mixing thread.
class ClipAudioRetimer
{
public:
    // Writes up to `frames` of interleaved stereo from the clip's source starting at sourceStartUs,
    // resampled to the block's sample rate. Returns frames actually written; 0 means end of source.
    using SourcePull =
        std::function<int(drift::TimeUs sourceStartUs, int frames, float *interleavedStereo)>;

    ClipAudioRetimer();
    ~ClipAudioRetimer();
    ClipAudioRetimer(ClipAudioRetimer &&other) noexcept;
    ClipAudioRetimer &operator=(ClipAudioRetimer &&other) noexcept;
    ClipAudioRetimer(const ClipAudioRetimer &) = delete;
    ClipAudioRetimer &operator=(const ClipAudioRetimer &) = delete;

    // Emits exactly `frames` of retimed interleaved stereo, pulling as much source as the stretcher
    // needs. Restarts the stream when `block` is discontinuous with the previous call. Frames past
    // the end of the source come back as silence.
    void process(const ClipAudioBlock &block, const SourcePull &pull, int frames,
                 float *interleavedStereoOut);

    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace drift

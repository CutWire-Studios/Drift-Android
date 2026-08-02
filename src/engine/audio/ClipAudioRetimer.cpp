#include "engine/audio/ClipAudioRetimer.h"

#include "core/SpeedCurve.h"

#include <soundtouch/SoundTouch.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace drift {

namespace {

constexpr int kChannels = 2;

// Forward reads stay sequential, and ClipReader only re-seeks on a discontinuity, so a large chunk
// costs nothing and amortises the hop to the reader thread over several audio blocks.
constexpr int kForwardChunkFrames = 4096;

// Every reverse read is a backward jump past ClipReader's tolerance, so each one flushes the codec.
// Half a second turns a seek per audio block into a seek per couple of dozen.
constexpr double kReverseChunkSeconds = 0.5;

// How much source goes into the stretcher at once. This is the granularity at which a speed ramp is
// sampled, so it wants to stay well under one audio block's worth.
constexpr int kPushFrames = 1024;

// Bounds the priming pull. At the top of the speed range one block needs a second or two of source,
// which is the only case that comes near this.
constexpr int kMaxPullIterations = 256;

// Frame counts are converted through microseconds exactly the way ClipReader advances its own
// position, so the two cursors stay bit-identical. Accumulating from a running frame total instead
// loses a fraction of a microsecond per call and eventually forces a spurious seek.
drift::TimeUs framesToUs(int frames, int sampleRate)
{
    return static_cast<drift::TimeUs>(frames) * drift::kUsPerSecond / sampleRate;
}

int usToFrames(drift::TimeUs us, int sampleRate)
{
    return static_cast<int>((us * sampleRate) / drift::kUsPerSecond);
}

} // namespace

struct ClipAudioRetimer::Impl
{
    std::unique_ptr<soundtouch::SoundTouch> stretcher = std::make_unique<soundtouch::SoundTouch>();

    quint64 identity = 0;
    int sampleRate = 0;
    double tempo = -1.0;
    bool reverse = false;
    int reservoir = 0;
    bool flushed = false;
    drift::TimeUs lastTimelineEndUs = -1;
    drift::TimeUs srcCursorUs = 0;

    std::vector<float> staging; // interleaved source waiting to go into the stretcher
    size_t stagingRead = 0;
    std::vector<float> fifo; // interleaved output waiting to be emitted
    size_t fifoRead = 0;

    int stagingFrames() const { return static_cast<int>((staging.size() - stagingRead) / kChannels); }
    int fifoFrames() const { return static_cast<int>((fifo.size() - fifoRead) / kChannels); }

    void prepare(int rate);
    void restart(drift::TimeUs sourceStartUs);
    void applyTempo(double value);
    bool refillStaging(const SourcePull &pull);
    void pushStaging();
    void drainIntoFifo();
};

void ClipAudioRetimer::Impl::prepare(int rate)
{
    sampleRate = rate;
    stretcher->setSampleRate(static_cast<uint>(rate));
    stretcher->setChannels(kChannels);
    // Only tempo ever moves. Rate would resample and take the pitch with it, which is the whole
    // thing this exists to avoid.
    stretcher->setRate(1.0);
    stretcher->setPitch(1.0);
    // Quickseek trades correlation accuracy for speed and is audible on voice. Sequence and seek
    // window stay on auto: they are tempo-dependent, and a speed ramp changes tempo every block.
    stretcher->setSetting(SETTING_USE_QUICKSEEK, 0);
    tempo = -1.0;
    // Whatever is buffered belongs to the old rate, and so does the position it was read from.
    lastTimelineEndUs = -1;
}

void ClipAudioRetimer::Impl::restart(drift::TimeUs sourceStartUs)
{
    stretcher->clear(); // settings survive
    staging.clear();
    stagingRead = 0;
    fifo.clear();
    fifoRead = 0;
    flushed = false;
    srcCursorUs = qMax<drift::TimeUs>(0, sourceStartUs);
}

void ClipAudioRetimer::Impl::applyTempo(double value)
{
    // clip.speed comes back from project JSON without the range the setters enforce, so a zero or a
    // negative can reach here — inside SoundTouch that is a divide by zero.
    const double clamped = qBound(drift::kMinCurveSpeed, value, drift::kMaxCurveSpeed);
    if (std::abs(clamped - tempo) < 1e-9)
        return;

    tempo = clamped;
    stretcher->setTempo(clamped);
    // Both of these move with tempo, so they are only meaningful once it is set.
    reservoir = std::max(64, stretcher->getSetting(SETTING_NOMINAL_OUTPUT_SEQUENCE));
}

bool ClipAudioRetimer::Impl::refillStaging(const SourcePull &pull)
{
    staging.clear();
    stagingRead = 0;

    if (!reverse) {
        staging.resize(static_cast<size_t>(kForwardChunkFrames) * kChannels, 0.0f);
        const int got = pull(srcCursorUs, kForwardChunkFrames, staging.data());
        if (got <= 0) {
            staging.clear();
            return false;
        }
        staging.resize(static_cast<size_t>(got) * kChannels);
        srcCursorUs += framesToUs(got, sampleRate);
        return true;
    }

    const int chunkFrames = std::max(1, static_cast<int>(sampleRate * kReverseChunkSeconds));
    const int readFrames = std::min(chunkFrames, usToFrames(srcCursorUs, sampleRate));
    if (readFrames <= 0)
        return false;

    const drift::TimeUs readStartUs = srcCursorUs - framesToUs(readFrames, sampleRate);
    // Reading into the front and leaving the tail zeroed is what keeps a short read from shifting
    // the stream: buffer index i is source readStartUs + i either way, so after the flip the
    // missing frames land at the head of the segment, where they belong, instead of displacing
    // everything behind them.
    staging.resize(static_cast<size_t>(readFrames) * kChannels, 0.0f);
    const int got = pull(readStartUs, readFrames, staging.data());
    if (got <= 0) {
        staging.clear();
        return false;
    }
    std::fill(staging.begin() + static_cast<long>(got) * kChannels, staging.end(), 0.0f);

    for (int i = 0, j = readFrames - 1; i < j; ++i, --j) {
        std::swap(staging[static_cast<size_t>(i) * 2], staging[static_cast<size_t>(j) * 2]);
        std::swap(staging[static_cast<size_t>(i) * 2 + 1], staging[static_cast<size_t>(j) * 2 + 1]);
    }
    srcCursorUs = readStartUs;
    return true;
}

void ClipAudioRetimer::Impl::pushStaging()
{
    const int frames = std::min(kPushFrames, stagingFrames());
    if (frames <= 0)
        return;

    stretcher->putSamples(staging.data() + stagingRead, static_cast<uint>(frames));
    stagingRead += static_cast<size_t>(frames) * kChannels;
    if (stagingRead >= staging.size()) {
        staging.clear();
        stagingRead = 0;
    }
    drainIntoFifo();
}

void ClipAudioRetimer::Impl::drainIntoFifo()
{
    for (;;) {
        const size_t used = fifo.size();
        fifo.resize(used + static_cast<size_t>(kPushFrames) * kChannels);
        const uint got = stretcher->receiveSamples(fifo.data() + used, kPushFrames);
        fifo.resize(used + static_cast<size_t>(got) * kChannels);
        if (got == 0)
            break;
    }
}

ClipAudioRetimer::ClipAudioRetimer()
    : m_impl(std::make_unique<Impl>())
{
}

ClipAudioRetimer::~ClipAudioRetimer() = default;
ClipAudioRetimer::ClipAudioRetimer(ClipAudioRetimer &&other) noexcept = default;
ClipAudioRetimer &ClipAudioRetimer::operator=(ClipAudioRetimer &&other) noexcept = default;

void ClipAudioRetimer::reset()
{
    m_impl->lastTimelineEndUs = -1;
    m_impl->restart(0);
}

void ClipAudioRetimer::process(const ClipAudioBlock &block, const SourcePull &pull, int frames,
                               float *interleavedStereoOut)
{
    if (!interleavedStereoOut || frames <= 0)
        return;

    std::memset(interleavedStereoOut, 0, static_cast<size_t>(frames) * kChannels * sizeof(float));
    if (block.sampleRate <= 0 || !pull)
        return;

    if (block.sampleRate != m_impl->sampleRate)
        m_impl->prepare(block.sampleRate);

    const bool continuous = m_impl->lastTimelineEndUs >= 0 && block.identity == m_impl->identity
                            && block.reverse == m_impl->reverse
                            && qAbs(block.timelineStartUs - m_impl->lastTimelineEndUs) <= 2'000;
    if (!continuous) {
        m_impl->identity = block.identity;
        m_impl->reverse = block.reverse;
        m_impl->restart(block.sourceStartUs);
    }
    m_impl->applyTempo(block.tempo);

    // The stretcher emits in batches, so opening the stream one batch ahead is what keeps a short
    // batch from punching a hole later. Once it is running, one block's worth is enough. Nothing is
    // discarded: the first sample pushed is the first sample out, so there is no latency to prime
    // away and no need to read source from before the clip's in point.
    const int target = m_impl->fifoFrames() == 0 ? frames + m_impl->reservoir : frames;
    for (int i = 0; i < kMaxPullIterations && m_impl->fifoFrames() < target; ++i) {
        if (m_impl->stagingFrames() == 0 && !m_impl->refillStaging(pull)) {
            // End of source. Whatever is still inside the stretcher is the clip's last few tens of
            // milliseconds, so it gets padded out rather than dropped — once, or the padding would
            // keep being appended for the rest of the clip.
            if (!m_impl->flushed) {
                m_impl->flushed = true;
                m_impl->stretcher->flush();
                m_impl->drainIntoFifo();
            }
            break;
        }
        m_impl->pushStaging();
    }

    const int emitFrames = std::min(frames, m_impl->fifoFrames());
    if (emitFrames > 0) {
        std::memcpy(interleavedStereoOut, m_impl->fifo.data() + m_impl->fifoRead,
                    static_cast<size_t>(emitFrames) * kChannels * sizeof(float));
        m_impl->fifoRead += static_cast<size_t>(emitFrames) * kChannels;
    }

    if (m_impl->fifoRead >= m_impl->fifo.size()) {
        m_impl->fifo.clear();
        m_impl->fifoRead = 0;
    } else if (m_impl->fifoRead >= static_cast<size_t>(kPushFrames) * kChannels * 4) {
        m_impl->fifo.erase(m_impl->fifo.begin(), m_impl->fifo.begin() + static_cast<long>(m_impl->fifoRead));
        m_impl->fifoRead = 0;
    }

    m_impl->lastTimelineEndUs = block.timelineStartUs + framesToUs(frames, block.sampleRate);
}

} // namespace drift

#include "AudioMixer.h"

#include "AudioAtempo.h"
#include "AudioEffectCatalog.h"
#include "ClipReaderPool.h"
#include "TransitionCatalog.h"
#include "core/Clip.h"
#include "core/Transition.h"

#include <QtMath>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>

namespace {

// Summing several clips, each with up to 2.0 of gain, regularly overshoots. Clamping squared off
// the peaks; this rounds them instead, asymptotically approaching full scale so the result can
// never exceed 1.0 however hard the mix is driven. Stateless — no attack, no release, no pumping
// across the timeline, nothing to reset on seek.
//
// The knee sits just under full scale on purpose. Anything lower colours audio that was never
// going to clip: normal material crosses -3 dBFS on every peak, so a knee down there is an
// always-on waveshaper rather than a safety net.
constexpr float kSoftClipKnee = 0.95f; // -0.45 dBFS

float softClip(float sample)
{
    const float magnitude = std::fabs(sample);
    if (magnitude <= kSoftClipKnee)
        return sample;

    const float over = (magnitude - kSoftClipKnee) / (1.0f - kSoftClipKnee);
    const float shaped = kSoftClipKnee + (1.0f - kSoftClipKnee) * std::tanh(over);
    return std::copysign(shaped, sample);
}

double volumeForClip(const drift::Clip &clip, drift::TimeUs timelineUs)
{
    if (clip.volume.isEmpty())
        return 1.0;
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, timelineUs - clip.timelineStart);
    return qBound(0.0, clip.volume.evaluateAt(relative), 2.0);
}

double transitionGainForClip(const drift::Track &track, const drift::Clip &clip, drift::TimeUs timelineUs)
{
    drift::TimeUs windowStart = 0;
    drift::TimeUs windowEnd = 0;
    const drift::Transition *transition = drift::activeTransitionAt(track, timelineUs, windowStart, windowEnd);
    if (!transition)
        return 1.0;

    const double p = drift::transitionProgress(timelineUs, windowStart, windowEnd);
    const TransitionPresetEntry *def = transitionDefForId(transition->kindId);
    const QString curve = def ? def->audioCurve : QStringLiteral("crossfade");
    const drift::TransitionAudioGains gains = drift::transitionAudioGains(curve, p);
    if (clip.id == transition->fromClipId)
        return gains.outgoing;
    if (clip.id == transition->toClipId)
        return gains.incoming;
    return 1.0;
}

constexpr drift::TimeUs kTimelineGapToleranceUs = 2'000; // ~2 ms: allow frame rounding between blocks

} // namespace

// Silence outside the clip means this is safe to call for a preroll window that runs off the
// clip's front edge.
QVector<float> AudioMixer::readClipAudio(const drift::Clip &clip, drift::TimeUs winStartUs, int outFrames,
                                         int sampleRate)
{
    QVector<float> out(outFrames * 2, 0.0f);
    if (outFrames <= 0)
        return out;

    const drift::TimeUs winDurUs =
        static_cast<drift::TimeUs>((static_cast<int64_t>(outFrames) * drift::kUsPerSecond) / sampleRate);
    const drift::TimeUs winEndUs = winStartUs + winDurUs;
    if (winEndUs <= clip.timelineStart || winStartUs >= clip.timelineEnd())
        return out; // window is entirely outside the clip — pure silence

    // Clamp the window to the clip; frames before the clip's start stay as the leading zeros above.
    const drift::TimeUs playStartUs = qMax(winStartUs, clip.timelineStart);
    const int leadFrames = static_cast<int>(((playStartUs - winStartUs) * sampleRate) / drift::kUsPerSecond);
    const int wantFrames = qMax(1, outFrames - leadFrames);

    // A ramp only means the rate changes from block to block; within one block it is read as
    // constant, which is what atempo can express anyway. Taken from the curve rather than by
    // differencing two mapped positions so the final, partly-overhanging block of a clip still
    // reports its true rate.
    const double speed =
        clip.hasSpeedCurve()
            ? clip.speedCurve.speedAtTimelineOffset(playStartUs - clip.timelineStart,
                                                    clip.srcOut - clip.srcIn)
            : clip.effectiveSpeed();

    // Frame counts are derived in the sample domain, never by going through microseconds. A block
    // whose duration is not a whole number of microseconds — 1024 frames at 48 kHz is 21333.33 —
    // used to truncate twice, once into µs and once back out, and ask the decoder for 1023 frames
    // to fill 1024. The frame left behind stayed at the buffer's initial zero, putting a
    // single-sample dropout on every block boundary: a periodic impulse, which is a harmonic comb
    // all the way to Nyquist.
    const int sourceSampleCount = qMax(1, static_cast<int>(std::llround(wantFrames * speed)));
    const drift::TimeUs sourceSpanUs = qMax<drift::TimeUs>(
        1, static_cast<drift::TimeUs>((static_cast<int64_t>(sourceSampleCount) * drift::kUsPerSecond)
                                      / sampleRate));

    // Reverse reads the block ahead of the mapped position and flips it below.
    const drift::TimeUs sourceStartUs =
        clip.reverse ? qMax<drift::TimeUs>(0, clip.timelineToSourceUs(playStartUs) - sourceSpanUs)
                     : clip.timelineToSourceUs(playStartUs);

    QVector<float> sourceChunk(sourceSampleCount * 2);
    const int got = ClipReaderPool::instance().readAudioInterleaved(clip.path, sourceStartUs, sourceSampleCount,
                                                                    sampleRate, sourceChunk.data());
    if (got <= 0)
        return out;

    if (clip.reverse && got > 1) {
        for (int i = 0, j = got - 1; i < j; ++i, --j) {
            std::swap(sourceChunk[i * 2], sourceChunk[j * 2]);
            std::swap(sourceChunk[i * 2 + 1], sourceChunk[j * 2 + 1]);
        }
    }

    QVector<float> chunk;
    if (qFuzzyCompare(speed, 1.0))
        chunk = sourceChunk;
    else
        chunk = AudioAtempo::apply(sourceChunk.constData(), got, sampleRate, speed, wantFrames);

    const int copyFrames = qMin(wantFrames, chunk.size() / 2);
    for (int i = 0; i < copyFrames && (leadFrames + i) < outFrames; ++i) {
        out[(leadFrames + i) * 2] = chunk[i * 2];
        out[(leadFrames + i) * 2 + 1] = chunk[i * 2 + 1];
    }
    return out;
}

namespace {

void accumulateClipAudio(const drift::Clip &clip, const drift::Track &track, drift::TimeUs timelineStartUs,
                         int sampleCount, int sampleRate, float *mixBuffer,
                         QMutex &rackMutex,
                         QHash<QString, std::shared_ptr<drift::AudioEffectRack>> &effectRacks)
{
    if (clip.path.isEmpty())
        return;

    const drift::TimeUs bufferEndUs = timelineStartUs + static_cast<drift::TimeUs>(
                                                            (static_cast<int64_t>(sampleCount) * drift::kUsPerSecond)
                                                            / sampleRate);

    const bool overlaps = clip.containsTime(timelineStartUs) || clip.containsTime(bufferEndUs - 1)
                          || (timelineStartUs < clip.timelineStart && bufferEndUs > clip.timelineEnd());
    if (!overlaps)
        return;

    const drift::TimeUs blockDurUs = static_cast<drift::TimeUs>(
        (static_cast<int64_t>(sampleCount) * drift::kUsPerSecond) / sampleRate);

    QVector<float> chunk;
    if (!clip.audioEffects.isEmpty()) {
        // Hold a strong reference rather than pointing into the hash. mix() runs on the audio
        // thread while resetEffectRacks() clears this hash from the GUI thread on every seek, play
        // and pause: an unguarded operator[] can rehash underneath that clear(), and a reference
        // into the hash dangles the moment clear() drops the last owner — the rack's buffers are
        // then freed while this thread is still processing into them.
        std::shared_ptr<drift::AudioEffectRack> rackPtr;
        {
            QMutexLocker locker(&rackMutex);
            rackPtr = effectRacks.value(clip.id);
            if (!rackPtr) {
                rackPtr = std::make_shared<drift::AudioEffectRack>();
                effectRacks.insert(clip.id, rackPtr);
            }
        }
        // Safe even if the hash is cleared right now: this copy keeps the rack alive until the
        // block finishes, and the next block simply builds a fresh one.
        drift::AudioEffectRack &rack = *rackPtr;

        const drift::TimeUs lastEndUs = rack.lastTimelineEndUs();
        const bool continuous = lastEndUs >= 0
                                && qAbs(timelineStartUs - lastEndUs) <= kTimelineGapToleranceUs;

        const bool active = rack.configure(audioEffectSpecsFor(clip.audioEffects), sampleRate);
        if (active && !continuous) {
            rack.reset();
            // Warm the stages on the audio immediately before this block. That is what makes an
            // echo tail already present after a seek instead of fading in from silence, and what
            // lines up a latent stage instead of leaving it permanently late.
            const int primeFrames = rack.primeFrames();
            if (primeFrames > 0) {
                const drift::TimeUs primeStartUs =
                    timelineStartUs
                    - static_cast<drift::TimeUs>((static_cast<int64_t>(primeFrames) * drift::kUsPerSecond)
                                                 / sampleRate);
                const QVector<float> preroll =
                    AudioMixer::readClipAudio(clip, primeStartUs, primeFrames, sampleRate);
                rack.warmUp(preroll.constData(), primeFrames);
            }
        }

        chunk = AudioMixer::readClipAudio(clip, timelineStartUs, sampleCount, sampleRate);
        if (active)
            rack.process(chunk.data(), sampleCount);
        rack.setLastTimelineEndUs(timelineStartUs + blockDurUs);
    } else {
        chunk = AudioMixer::readClipAudio(clip, timelineStartUs, sampleCount, sampleRate);
    }

    const int frames = qMin(sampleCount, chunk.size() / 2);
    for (int i = 0; i < frames; ++i) {
        const drift::TimeUs sampleTimeUs =
            timelineStartUs + static_cast<drift::TimeUs>((static_cast<int64_t>(i) * drift::kUsPerSecond) / sampleRate);
        const float gain = static_cast<float>(volumeForClip(clip, sampleTimeUs)
                                              * transitionGainForClip(track, clip, sampleTimeUs)
                                              * clip.fadeMultiplier(sampleTimeUs));
        mixBuffer[i * 2] += chunk[i * 2] * gain;
        mixBuffer[i * 2 + 1] += chunk[i * 2 + 1] * gain;
    }
}

} // namespace

void AudioMixer::setProject(const drift::Project *project)
{
    if (m_project != project) {
        QMutexLocker locker(&m_effectRackMutex);
        m_effectRacks.clear();
    }
    m_project = project;
}

void AudioMixer::resetEffectRacks()
{
    QMutexLocker locker(&m_effectRackMutex);
    m_effectRacks.clear();
}

void AudioMixer::mix(drift::TimeUs timelineStartUs, int sampleCount, int sampleRate,
                     float *interleavedStereoOut) const
{
    if (!interleavedStereoOut || sampleCount <= 0 || !m_project)
        return;

    std::memset(interleavedStereoOut, 0, static_cast<size_t>(sampleCount) * 2 * sizeof(float));

    for (const drift::Track &track : m_project->tracks()) {
        if (track.muted || track.hidden)
            continue;

        if (track.type == drift::TrackType::Audio) {
            for (const drift::Clip &clip : track.clips)
                accumulateClipAudio(clip, track, timelineStartUs, sampleCount, sampleRate,
                                      interleavedStereoOut, m_effectRackMutex, m_effectRacks);
        } else if (track.type == drift::TrackType::Video) {
            for (const drift::Clip &clip : track.clips) {
                if (clip.type == drift::ClipType::Video && !clip.suppressEmbeddedAudio)
                    accumulateClipAudio(clip, track, timelineStartUs, sampleCount, sampleRate,
                                          interleavedStereoOut, m_effectRackMutex, m_effectRacks);
            }
        }
    }

    for (int i = 0; i < sampleCount * 2; ++i)
        interleavedStereoOut[i] = softClip(interleavedStereoOut[i]);
}

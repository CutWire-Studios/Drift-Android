#include "PlaybackClock.h"

namespace {

// The sink only reports a played position once audio actually flows, and opening
// its stream plus pulling the first buffer takes a few hundred ms — longer when
// that first buffer has to open a multi-hour file. Advancing the playhead on wall
// time during that window and then correcting to the sink's position snapped it
// back to where playback started, which restarted the preview from there. Hold at
// the start position instead, and only advance on wall time past this grace period,
// for the case where the sink never reports progress at all (no audio device).
constexpr qint64 kSinkSyncGraceNs = 500'000'000;

} // namespace

qint64 PlaybackClock::nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void PlaybackClock::reset(drift::TimeUs playheadUs, int sampleRate)
{
    m_pausedAtUs = qMax<drift::TimeUs>(0, playheadUs);
    m_startPlayheadUs = m_pausedAtUs;
    m_sampleRate = qMax(1, sampleRate);
    m_audioSamplesRendered.store(0, std::memory_order_release);
    m_running.store(false, std::memory_order_release);

    QMutexLocker lock(&m_anchorMutex);
    m_anchorPlayedUs = 0;
    m_anchorWallNs = 0;
    m_lastReportedUs = m_pausedAtUs;
}

void PlaybackClock::start()
{
    m_startPlayheadUs = m_pausedAtUs;
    m_audioSamplesRendered.store(0, std::memory_order_release);
    {
        QMutexLocker lock(&m_anchorMutex);
        m_anchorPlayedUs = 0;
        m_anchorWallNs = 0;
        m_startWallNs = nowNs();
        m_lastReportedUs = m_startPlayheadUs;
    }
    m_running.store(true, std::memory_order_release);
}

void PlaybackClock::pause()
{
    if (!m_running.load(std::memory_order_acquire))
        return;

    m_pausedAtUs = currentTimeUs();
    m_running.store(false, std::memory_order_release);
}

void PlaybackClock::stop()
{
    pause();
    m_audioSamplesRendered.store(0, std::memory_order_release);
}

void PlaybackClock::setRate(double rate)
{
    m_rate = qBound(0.1, rate, 8.0);
}

drift::TimeUs PlaybackClock::produceTimeUs() const
{
    if (!m_running.load(std::memory_order_acquire))
        return m_pausedAtUs;

    return m_startPlayheadUs + static_cast<drift::TimeUs>(m_rate * static_cast<double>(renderedFramesUs()));
}

drift::TimeUs PlaybackClock::renderedFramesUs() const
{
    const int64_t samples = m_audioSamplesRendered.load(std::memory_order_acquire);
    return static_cast<drift::TimeUs>((samples * drift::kUsPerSecond) / m_sampleRate);
}

void PlaybackClock::onAudioSamplesRendered(int sampleCount)
{
    if (sampleCount <= 0)
        return;
    m_audioSamplesRendered.fetch_add(sampleCount, std::memory_order_acq_rel);
}

void PlaybackClock::syncPlaybackUs(drift::TimeUs playedUs)
{
    QMutexLocker lock(&m_anchorMutex);
    m_anchorPlayedUs = qMax<drift::TimeUs>(0, playedUs);
    m_anchorWallNs = nowNs();
}

drift::TimeUs PlaybackClock::currentTimeUs() const
{
    if (!m_running.load(std::memory_order_acquire))
        return m_pausedAtUs;

    QMutexLocker lock(&m_anchorMutex);
    drift::TimeUs positionUs;
    // Both branches measure real time played and scale it into timeline time, so a rate other than
    // 1 moves the playhead at that multiple without any of the sync logic changing shape.
    if (m_anchorWallNs == 0) { // no sink sync yet
        const qint64 sinceStartNs = nowNs() - m_startWallNs;
        positionUs = sinceStartNs > kSinkSyncGraceNs
                         ? m_startPlayheadUs
                             + static_cast<drift::TimeUs>(m_rate * (sinceStartNs - kSinkSyncGraceNs)) / 1000
                         : m_startPlayheadUs;
    } else {
        const drift::TimeUs interpUs = (nowNs() - m_anchorWallNs) / 1000;
        const drift::TimeUs playedUs = m_anchorPlayedUs + qMax<drift::TimeUs>(0, interpUs);
        positionUs = m_startPlayheadUs + static_cast<drift::TimeUs>(m_rate * static_cast<double>(playedUs));
    }

    // The visible playhead must never run backwards while playing. Any correction
    // that would move it back — a late first sync, a sink that stalls and catches
    // up — holds it still until the true position passes it again.
    m_lastReportedUs = qMax(m_lastReportedUs, positionUs);
    return m_lastReportedUs;
}

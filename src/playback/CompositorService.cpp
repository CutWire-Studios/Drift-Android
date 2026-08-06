#include "CompositorService.h"

#include <QMetaType>
#include <cmath>

namespace {
#ifdef Q_OS_ANDROID
// How late a finished frame may be and still be worth showing. A phone can miss the
// desktop 100 ms deadline on every single frame, and dropping them all leaves the
// playhead running over a frozen canvas, which is worse than a late picture.
constexpr drift::TimeUs kMaxPreviewFrameStalenessUs = 1'000'000;
#else
constexpr drift::TimeUs kMaxPreviewFrameStalenessUs = 100'000;
#endif
// How late a frame may be before the adaptive scaler treats it as a miss. Kept at the
// desktop deadline on every platform: tying it to the display allowance above meant a
// phone one second behind still counted as on time and never stepped its scale down.
constexpr drift::TimeUs kAdaptiveLatenessUs = 100'000;
constexpr double kAdaptiveScaleMin = 0.25;
constexpr double kAdaptiveScaleStepDown = 0.75;
constexpr double kAdaptiveScaleStepUp = 1.15;
constexpr int kStaleBeforeScaleDown = 2;
constexpr int kOnTimeBeforeScaleUp = 8;
}

CompositorWorker::CompositorWorker(QObject *parent)
    : QObject(parent)
{
}

void CompositorWorker::composite(drift::TimeUs timeUs, FrameCompositor::RenderOptions options,
                                 std::shared_ptr<const drift::Project> snapshot)
{
    // Keep the shared tree alive for the whole frame; setProject only borrows.
    m_snapshot = std::move(snapshot);
    if (!m_snapshot) {
        // Still report completion: the service treats a request as in flight
        // until the worker answers, and the quality-mode play loop waits for it.
        emit frameReady(GpuFrameTexture{}, timeUs);
        return;
    }
    m_compositor.setProject(m_snapshot.get());

    emit frameReady(m_compositor.compositeToTextureAt(timeUs, options), timeUs);
}

CompositorService::CompositorService(QObject *parent)
    : QObject(parent)
    , m_worker(new CompositorWorker)
{
    qRegisterMetaType<drift::TimeUs>("drift::TimeUs");
    qRegisterMetaType<std::shared_ptr<const drift::Project>>("std::shared_ptr<const drift::Project>");
    qRegisterMetaType<FrameCompositor::RenderOptions>("FrameCompositor::RenderOptions");
    qRegisterMetaType<GpuFrameTexture>("GpuFrameTexture");
    m_worker->moveToThread(&m_thread);
    connect(m_worker, &CompositorWorker::frameReady, this, &CompositorService::onWorkerFrameReady,
            Qt::QueuedConnection);
    m_thread.start();
}

CompositorService::~CompositorService()
{
    m_thread.quit();
    m_thread.wait();
    delete m_worker;
    m_worker = nullptr;
}

void CompositorService::setProject(const drift::Project *project)
{
    m_project = project;
    invalidateSnapshot();
}

void CompositorService::invalidateSnapshot()
{
    ++m_liveGeneration;
    m_sharedSnapshot.reset();
}

void CompositorService::setDropLateFrames(bool drop)
{
    if (m_dropLateFrames == drop)
        return;
    m_dropLateFrames = drop;
    // Quality mode renders every frame at the requested scale; leaving a
    // downscale from a previous fast-mode run would defeat the point.
    m_adaptiveScale = 1.0;
    m_staleStreak = 0;
    m_onTimeStreak = 0;
}

double CompositorService::adaptiveScaleFactor() const
{
    return m_adaptiveScale;
}

FrameCompositor::RenderOptions CompositorService::effectiveOptions(
    FrameCompositor::RenderOptions options) const
{
    options.previewScale =
        qBound(0.1, options.previewScale * m_adaptiveScale, 1.0);
    return options;
}

void CompositorService::noteFrameStale(bool stale)
{
    if (stale) {
        m_onTimeStreak = 0;
        ++m_staleStreak;
        if (m_staleStreak >= kStaleBeforeScaleDown && m_adaptiveScale > kAdaptiveScaleMin + 1e-6) {
            m_adaptiveScale = qMax(kAdaptiveScaleMin, m_adaptiveScale * kAdaptiveScaleStepDown);
            m_staleStreak = 0;
        }
        return;
    }

    m_staleStreak = 0;
    ++m_onTimeStreak;
    if (m_onTimeStreak >= kOnTimeBeforeScaleUp && m_adaptiveScale < 1.0 - 1e-6) {
        m_adaptiveScale = qMin(1.0, m_adaptiveScale * kAdaptiveScaleStepUp);
        m_onTimeStreak = 0;
    }
}

void CompositorService::dispatch(drift::TimeUs timeUs, const FrameCompositor::RenderOptions &options)
{
    if (!m_project)
        return;

    if (!m_sharedSnapshot || m_snapshotGeneration != m_liveGeneration) {
        // One uniquely-owned snapshot per generation; subsequent ticks only bump
        // the shared_ptr. Plain Project copy would keep sharing QMap/QList with
        // the live tree — unsafe once the GUI mutates while the worker reads.
        m_sharedSnapshot = std::make_shared<drift::Project>(m_project->detachedCopy());
        m_snapshotGeneration = m_liveGeneration;
    }

    QMetaObject::invokeMethod(m_worker, "composite", Qt::QueuedConnection,
                              Q_ARG(drift::TimeUs, timeUs),
                              Q_ARG(FrameCompositor::RenderOptions, options),
                              Q_ARG(std::shared_ptr<const drift::Project>, m_sharedSnapshot));
}

void CompositorService::requestComposite(drift::TimeUs timeUs, FrameCompositor::RenderOptions options)
{
    options.previewScale = qBound(0.1, options.previewScale, 1.0);
    options = effectiveOptions(options);
    m_pendingTimeUs.store(timeUs, std::memory_order_release);
    m_pendingPreviewScalePercent.store(
        qBound(10, static_cast<int>(std::lround(options.previewScale * 100.0)), 100),
        std::memory_order_release);
    m_pendingMaxTimeEchoHistoryFrames.store(options.maxTimeEchoHistoryFrames, std::memory_order_release);
    m_pendingReadAheadUs.store(options.readAheadUs, std::memory_order_release);
    if (m_requestPending.exchange(true, std::memory_order_acq_rel))
        return;

    m_lastDispatchedTimeUs = timeUs;
    m_lastDispatchedOptions = options;
    dispatch(timeUs, options);
}

void CompositorService::onWorkerFrameReady(const GpuFrameTexture &frame, drift::TimeUs timeUs)
{
    const drift::TimeUs latest = m_pendingTimeUs.load(std::memory_order_acquire);
    FrameCompositor::RenderOptions latestOptions;
    latestOptions.previewScale =
        static_cast<double>(m_pendingPreviewScalePercent.load(std::memory_order_acquire)) / 100.0;
    latestOptions.maxTimeEchoHistoryFrames =
        m_pendingMaxTimeEchoHistoryFrames.load(std::memory_order_acquire);
    latestOptions.readAheadUs = m_pendingReadAheadUs.load(std::memory_order_acquire);

    // Quality mode shows every frame it renders, however far behind the request
    // it finished; only fast mode discards frames the playhead has run past.
    const drift::TimeUs lateBy = latest > timeUs ? latest - timeUs : 0;
    const bool stale = m_dropLateFrames && lateBy > kMaxPreviewFrameStalenessUs;
    if (m_dropLateFrames)
        noteFrameStale(lateBy > kAdaptiveLatenessUs);
    if (!stale && frame.isValid())
        emit frameReady(frame);

    m_requestPending.store(false, std::memory_order_release);

    if (latest != m_lastDispatchedTimeUs
        || latestOptions.previewScale != m_lastDispatchedOptions.previewScale
        || latestOptions.maxTimeEchoHistoryFrames != m_lastDispatchedOptions.maxTimeEchoHistoryFrames
        || latestOptions.readAheadUs != m_lastDispatchedOptions.readAheadUs) {
        m_lastDispatchedTimeUs = latest;
        m_lastDispatchedOptions = latestOptions;
        if (!m_requestPending.exchange(true, std::memory_order_acq_rel))
            dispatch(latest, latestOptions);
    }

    // Last: a listener may start the next composite from here, and that request
    // must not be overwritten by the catch-up dispatch above.
    emit compositeFinished();
}

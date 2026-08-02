#pragma once

#include "engine/FrameCompositor.h"
#include "engine/GpuCompositor.h"

#include <QObject>
#include <QThread>

#include <atomic>
#include <memory>

class CompositorWorker : public QObject
{
    Q_OBJECT

public:
    explicit CompositorWorker(QObject *parent = nullptr);

public slots:
    // Shared immutable snapshot taken on the GUI thread. The worker must never
    // hold a pointer into the live project: compositing runs concurrently with
    // editing, and reading a QMap/QList while the GUI thread rebalances it is a
    // use-after-free. Use Project::detachedCopy() so Qt COW payloads are unique,
    // then share that snapshot via shared_ptr across queued invokes.
    void composite(drift::TimeUs timeUs, FrameCompositor::RenderOptions options,
                   std::shared_ptr<const drift::Project> snapshot);

signals:
    void frameReady(const GpuFrameTexture &frame, drift::TimeUs timeUs);

private:
    FrameCompositor m_compositor;
    std::shared_ptr<const drift::Project> m_snapshot;
};

// Background compositor thread. Frames are delivered to the GUI as live GL
// textures out of the runtime's presentation ring — never read back to the CPU
// and never re-uploaded. The ring is what keeps the scene graph from sampling a
// target that is being drawn into, so there is no frame buffering here.
class CompositorService : public QObject
{
    Q_OBJECT

public:
    explicit CompositorService(QObject *parent = nullptr);
    ~CompositorService() override;

    void setProject(const drift::Project *project);
    void requestComposite(drift::TimeUs timeUs,
                          FrameCompositor::RenderOptions options = FrameCompositor::RenderOptions{});

    // Fast playback (the default) drops frames that finish after the playhead has
    // moved on and downscales when it keeps happening. Turn this off to present
    // every rendered frame at the requested scale, however long it takes.
    void setDropLateFrames(bool drop);

    // Multiplier applied on top of the caller's previewScale during playback
    // overload (1.0 = full requested quality). Driven by stale-frame feedback.
    double adaptiveScaleFactor() const;

signals:
    void frameReady(const GpuFrameTexture &frame);
    // One per completed request, whether or not a frame was produced or shown.
    void compositeFinished();

private slots:
    void onWorkerFrameReady(const GpuFrameTexture &frame, drift::TimeUs timeUs);

private:
    void dispatch(drift::TimeUs timeUs, const FrameCompositor::RenderOptions &options);
    void noteFrameStale(bool stale);
    FrameCompositor::RenderOptions effectiveOptions(FrameCompositor::RenderOptions options) const;

    const drift::Project *m_project = nullptr;
    // Reused across ticks until the live project pointer changes or the GUI
    // asks for a fresh snapshot after edits (generation bump via invalidateSnapshot).
    std::shared_ptr<const drift::Project> m_sharedSnapshot;
    int m_snapshotGeneration = 0;
    int m_liveGeneration = 0;

    std::atomic<bool> m_requestPending{false};
    std::atomic<drift::TimeUs> m_pendingTimeUs{0};
    std::atomic<int> m_pendingPreviewScalePercent{100};
    std::atomic<int> m_pendingMaxTimeEchoHistoryFrames{-1};
    std::atomic<drift::TimeUs> m_pendingReadAheadUs{0};
    drift::TimeUs m_lastDispatchedTimeUs = -1;
    FrameCompositor::RenderOptions m_lastDispatchedOptions;

    // Adaptive quality: consecutive on-time frames recover; stale frames drop scale.
    int m_staleStreak = 0;
    int m_onTimeStreak = 0;
    double m_adaptiveScale = 1.0;
    bool m_dropLateFrames = true;

    QThread m_thread;
    CompositorWorker *m_worker = nullptr;

public:
    // Call when the live project has been mutated so the next dispatch recopies.
    void invalidateSnapshot();
};

Q_DECLARE_METATYPE(GpuFrameTexture)
Q_DECLARE_METATYPE(std::shared_ptr<const drift::Project>)

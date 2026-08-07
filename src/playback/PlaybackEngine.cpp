#include "PlaybackEngine.h"

#include "engine/AndroidUri.h"

#include <QSettings>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>

namespace {

#ifdef Q_OS_ANDROID

constexpr const char *kAudioFocusClass = "org/cutwire/drift/AudioFocus";

// The engine that owns preview audio. Focus loss and the headphone-unplug broadcast are dispatched
// on the Android UI thread, so the pause cannot be run there: it is posted to the engine's own
// thread instead. Playback is single-instance, so the last engine constructed is the right one.
std::atomic<PlaybackEngine *> g_audioFocusEngine{nullptr};

void nativePausePlayback(JNIEnv *, jclass)
{
    if (PlaybackEngine *engine = g_audioFocusEngine.load(std::memory_order_acquire))
        QMetaObject::invokeMethod(engine, &PlaybackEngine::pause, Qt::QueuedConnection);
}

// Both calls hop to the Android UI thread: AudioFocus keeps its request and its receiver in static
// fields that only that thread touches, which is also the thread the callbacks arrive on.
void callAudioFocus(const char *method)
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([method] {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid())
            return;
        QJniObject::callStaticMethod<void>(kAudioFocusClass, method,
                                           "(Landroid/content/Context;)V", context.object());
        // Focus is advisory: the preview still plays if the request could not be made, it just
        // plays over whatever else is running.
        QJniEnvironment().checkAndClearExceptions();
    });
}

void requestAudioFocus()
{
    callAudioFocus("request");
}

void abandonAudioFocus()
{
    callAudioFocus("abandon");
}

#else
inline void requestAudioFocus() {}
inline void abandonAudioFocus() {}
#endif

constexpr int kPlayheadUpdateMs = 16; // ~60 Hz UI updates, independent of video decode

// How much decoded source each clip's reader keeps buffered ahead of the
// playhead during fast playback. This absorbs frames that decode slower than
// realtime (long GOPs, a heavy transition) by spending the slack on either side
// of them. It is deliberately seconds and not minutes: the frames are held in
// RAM per clip — 2 s of 720p NV12 is ~83 MB — and every edit or seek discards
// the part of the buffer past the change.
constexpr drift::TimeUs kReadAheadUs = 2 * drift::kUsPerSecond;

// The rates the preview transport offers. All sit inside the stretcher's own clamp
// (kMinCurveSpeed..kMaxCurveSpeed), and nothing outside this list is accepted.
constexpr std::array<double, 6> kPlaybackRates{0.25, 0.5, 1.0, 1.5, 2.0, 4.0};

} // namespace

AudioPlaybackIODevice::AudioPlaybackIODevice(PlaybackEngine *engine, QObject *parent)
    : QIODevice(parent)
    , m_engine(engine)
{
    open(QIODevice::ReadOnly);
}

qint64 AudioPlaybackIODevice::readData(char *data, qint64 maxlen)
{
    if (!m_engine || maxlen <= 0)
        return 0;

    const int maxSamples = static_cast<int>(maxlen / (sizeof(float) * 2));
    if (maxSamples <= 0)
        return 0;

    float *buffer = reinterpret_cast<float *>(data);
    const int samples = m_engine->fillAudio(buffer, maxSamples);
    return static_cast<qint64>(samples) * static_cast<qint64>(sizeof(float) * 2);
}

qint64 AudioPlaybackIODevice::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);
    return -1;
}

qint64 AudioPlaybackIODevice::bytesAvailable() const
{
    return (1 << 20) + QIODevice::bytesAvailable();
}

PlaybackEngine::PlaybackEngine(QObject *parent)
    : QObject(parent)
    , m_device(new AudioPlaybackIODevice(this))
{
    const QString saved = QSettings().value(QStringLiteral("preview/quality")).toString().toLower();
    if (saved == QStringLiteral("full") || saved == QStringLiteral("half")
        || saved == QStringLiteral("quarter")) {
        m_previewQuality = saved;
    }

    const QString savedMode =
        QSettings().value(QStringLiteral("preview/playbackMode")).toString().toLower();
    if (savedMode == QStringLiteral("fast") || savedMode == QStringLiteral("quality"))
        m_playbackMode = savedMode;
    m_compositor.setDropLateFrames(!isQualityMode());

    // The pull device and sink run on a dedicated thread; the GUI thread stays
    // free while audio is decoded and mixed on refill.
    m_device->moveToThread(&m_audioThread);
    m_audioThread.setObjectName(QStringLiteral("PlaybackAudio"));
    m_audioThread.start();

    m_playheadTimer.setTimerType(Qt::PreciseTimer);
    m_compositeTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playheadTimer, &QTimer::timeout, this, &PlaybackEngine::onPlayheadTick);
    connect(&m_compositeTimer, &QTimer::timeout, this, &PlaybackEngine::onCompositeTick);
    connect(&m_compositor, &CompositorService::frameReady, this, &PlaybackEngine::onFrameReady);
    connect(&m_compositor, &CompositorService::compositeFinished, this,
            &PlaybackEngine::onCompositeFinished);

#ifdef Q_OS_ANDROID
    g_audioFocusEngine.store(this, std::memory_order_release);
    QJniEnvironment().registerNativeMethods(
        kAudioFocusClass,
        {{"nativePausePlayback", "()V", reinterpret_cast<void *>(nativePausePlayback)}});
#endif
}

PlaybackEngine::~PlaybackEngine()
{
#ifdef Q_OS_ANDROID
    // A focus change already in flight on the Android UI thread must not find this engine.
    g_audioFocusEngine.store(nullptr, std::memory_order_release);
    abandonAudioFocus();
#endif
    m_playing = false;
    m_playheadTimer.stop();
    m_compositeTimer.stop();
    m_clock.stop();

    // Tear down the sink on its own thread, then stop the thread and reclaim the
    // device (safe once the thread has finished).
    QMetaObject::invokeMethod(
        m_device,
        [this] {
            if (m_sink) {
                m_sink->stop();
                delete m_sink;
                m_sink = nullptr;
            }
        },
        Qt::BlockingQueuedConnection);
    m_audioThread.quit();
    m_audioThread.wait();
    delete m_device;
    m_device = nullptr;
}

void PlaybackEngine::ensureAudioSink()
{
    if (m_project)
        m_sampleRate = m_project->sampleRate();

    // Create the sink on the audio thread without blocking the GUI. play() only
    // Queued-starts it afterward; a missing sink for one refill period is fine.
    QMetaObject::invokeMethod(
        m_device,
        [this] {
            m_format.setSampleRate(m_sampleRate);
            m_format.setChannelCount(2);
            m_format.setSampleFormat(QAudioFormat::Float);

            if (!m_sink) {
                m_sink = new QAudioSink(m_format, m_device);
                // Keep the platform default buffer: a too-small buffer starves the
                // pull loop and makes playback run slow. The playhead is anchored to
                // the sink's played position, so buffer depth doesn't affect sync.
            }
        },
        Qt::QueuedConnection);
}

void PlaybackEngine::setProject(drift::Project *project)
{
    m_project = project;
    m_mixer.setProject(project);
    m_compositor.setProject(project);
    refreshFrame();
}

void PlaybackEngine::setPlayheadUs(drift::TimeUs us)
{
    m_playheadUs = qMax<drift::TimeUs>(0, us);
    // Only real seeks reach here — the playhead tick emits its position directly rather than
    // routing back through this setter. That matters: the mixer's per-clip DSP is streaming, and a
    // reset on every tick would leave a retimed clip permanently re-priming instead of playing.
    m_mixer.resetClipAudioState();
    m_audioStreamGeneration.fetch_add(1, std::memory_order_release);
    m_clock.reset(m_playheadUs, m_sampleRate);
    // reset() clears the running flag; resume the clock if we are still in play
    // so edits/seeks during playback don't freeze audio at one timeline spot.
    // Quality mode has no clock — its loop picks the new playhead up on the next
    // completed frame.
    if (!m_playing)
        refreshFrame();
    else if (!isQualityMode())
        m_clock.start();
}

int PlaybackEngine::previewTextureId() const
{
    QMutexLocker lock(&m_frameMutex);
    return static_cast<int>(m_currentFrame.textureId);
}

QSize PlaybackEngine::previewTextureSize() const
{
    QMutexLocker lock(&m_frameMutex);
    return m_currentFrame.size;
}

QImage PlaybackEngine::previewImage() const
{
    QMutexLocker lock(&m_frameMutex);
    return m_currentFrame.image;
}

QString PlaybackEngine::previewQuality() const
{
    return m_previewQuality;
}

void PlaybackEngine::setPreviewQuality(const QString &quality)
{
    const QString normalized = quality.toLower();
    // An unrecognized value used to silently become "half", which quietly changed
    // what the user was looking at. Ignore it instead.
    if (normalized != QStringLiteral("full") && normalized != QStringLiteral("half")
        && normalized != QStringLiteral("quarter")) {
        qWarning("PlaybackEngine: ignoring unknown preview quality '%s'", qPrintable(quality));
        return;
    }
    if (m_previewQuality == normalized)
        return;

    m_previewQuality = normalized;
    QSettings().setValue(QStringLiteral("preview/quality"), m_previewQuality);
    emit previewQualityChanged();
    refreshFrame();
}

QString PlaybackEngine::playbackMode() const
{
    return m_playbackMode;
}

void PlaybackEngine::setPlaybackMode(const QString &mode)
{
    const QString normalized = mode.toLower();
    if (normalized != QStringLiteral("fast") && normalized != QStringLiteral("quality")) {
        qWarning("PlaybackEngine: ignoring unknown playback mode '%s'", qPrintable(mode));
        return;
    }
    if (m_playbackMode == normalized)
        return;

    m_playbackMode = normalized;
    QSettings().setValue(QStringLiteral("preview/playbackMode"), m_playbackMode);
    m_compositor.setDropLateFrames(!isQualityMode());
    emit playbackModeChanged();

    // The two modes drive the playhead from different sources and differ on
    // whether the sink runs, so switching mid-playback restarts the transport
    // from where it currently sits.
    if (m_playing) {
        pause();
        play();
    }
}

void PlaybackEngine::setPlaybackRate(double rate)
{
    const auto match = std::find_if(kPlaybackRates.begin(), kPlaybackRates.end(),
                                    [rate](double candidate) { return qFuzzyCompare(candidate, rate); });
    if (match == kPlaybackRates.end()) {
        qWarning("PlaybackEngine: ignoring unsupported playback rate %f", rate);
        return;
    }
    if (qFuzzyCompare(m_playbackRate, *match))
        return;

    // Every position the clock reports is derived from its total rendered sample count, so a rate
    // applied mid-flight would rescale audio that has already been played. Restart the transport
    // from where it sits instead, exactly as a mode change does. Pausing first is also what makes
    // the assignment safe: fillAudio reads the rate on the audio thread, and pause() does not
    // return until the sink has stopped.
    const bool wasPlaying = m_playing;
    if (wasPlaying)
        pause();

    m_playbackRate = *match;
    emit playbackRateChanged();

    if (wasPlaying)
        play();
}

drift::TimeUs PlaybackEngine::frameStepUs() const
{
    return drift::frameDurationUs(m_project ? qMax(1, m_project->fps()) : 30);
}

void PlaybackEngine::setPreviewRenderSize(int width, int height)
{
    width = qMax(0, width);
    height = qMax(0, height);
    if (m_previewRenderWidth == width && m_previewRenderHeight == height)
        return;

    m_previewRenderWidth = width;
    m_previewRenderHeight = height;
    if (m_playing)
        onCompositeTick();
    else
        refreshFrame();
}

bool PlaybackEngine::hasFrame() const
{
    QMutexLocker lock(&m_frameMutex);
    return m_currentFrame.isValid();
}

void PlaybackEngine::play()
{
    if (m_playing)
        return;

    m_mixer.resetClipAudioState();
    m_audioStreamGeneration.fetch_add(1, std::memory_order_release);
    m_clock.setRate(m_playbackRate);
    m_clock.reset(m_playheadUs, m_sampleRate);
    m_playing = true;
    drift::android::acquireKeepScreenOn();

    if (isQualityMode()) {
        // Quality mode is not realtime: the playhead steps one frame per
        // completed composite, so there is no clock for audio to follow and the
        // sink stays stopped. The loop re-arms itself from onCompositeFinished.
        emit playingChanged();
        m_qualityRequestUs = m_playheadUs;
        m_compositor.requestComposite(m_playheadUs, playbackRenderOptions());
        return;
    }

    // Only the realtime path makes sound — quality mode leaves the sink stopped — and taking focus
    // for a silent render would interrupt whatever the user is listening to for nothing.
    requestAudioFocus();
    ensureAudioSink();
    m_clock.start();

    // Never block the GUI on sink I/O. start() may pull the first audio buffer,
    // which opens/seeks media — that must stay on the audio thread only.
    QMetaObject::invokeMethod(
        m_device,
        [this] {
            if (m_sink)
                m_sink->start(m_device);
        },
        Qt::QueuedConnection);

    emit playingChanged();

    m_playheadTimer.start(kPlayheadUpdateMs);

    const int fps = m_project ? qMax(1, m_project->fps()) : 30;
    // This is a display cadence, not a timeline one: a wall second should show about fps frames
    // whatever the rate, and above 1x that simply means covering more timeline per frame. Below 1x
    // the same interval would re-request the frame already on screen several times over, so the
    // tick stretches with the rate instead.
    const double frameMs = drift::usToSeconds(drift::frameDurationUs(fps)) * 1000.0;
    const int tickMs = qMax(1, static_cast<int>(frameMs * qMax(1.0, 1.0 / m_playbackRate)));
    m_compositeTimer.start(tickMs);

    onPlayheadTick();
    onCompositeTick();
}

void PlaybackEngine::pause()
{
    if (!m_playing)
        return;

    m_playing = false;
    drift::android::releaseKeepScreenOn();
    abandonAudioFocus();
    m_playheadTimer.stop();
    m_compositeTimer.stop();
    m_clock.pause();
    // In quality mode the frame loop owns the playhead; the clock never ran.
    if (!isQualityMode())
        m_playheadUs = m_clock.pausedAt();
    m_qualityRequestUs = -1;
    m_mixer.resetClipAudioState();
    m_audioStreamGeneration.fetch_add(1, std::memory_order_release);
    QMetaObject::invokeMethod(
        m_device,
        [this] {
            if (m_sink)
                m_sink->stop();
        },
        Qt::BlockingQueuedConnection);
    emit playingChanged();
    emit playheadUsChanged(static_cast<quint64>(m_playheadUs));
    refreshFrame();
}

void PlaybackEngine::refreshFrame()
{
    // Edits / seeks take a fresh project snapshot; the play loop reuses one.
    m_compositor.invalidateSnapshot();
    // Use the same RenderOptions as the play loop so paused/scrubbed frames
    // match what playback shows (preview scale + temporal-effect history).
    m_compositor.requestComposite(m_playheadUs, playbackRenderOptions());
}

void PlaybackEngine::checkEndOfTimeline(drift::TimeUs timeUs)
{
    if (!m_project)
        return;

    const drift::TimeUs durationUs = m_project->durationUs();
    if (timeUs >= durationUs) {
        m_playheadUs = durationUs;
        emit playheadUsChanged(static_cast<quint64>(m_playheadUs));
        QMetaObject::invokeMethod(this, &PlaybackEngine::pause, Qt::QueuedConnection);
    }
}

void PlaybackEngine::onPlayheadTick()
{
    if (!m_playing || !m_project)
        return;

    const drift::TimeUs timeUs = m_clock.currentTimeUs();
    if (timeUs == m_playheadUs)
        return;

    m_playheadUs = timeUs;
    emit playheadUsChanged(static_cast<quint64>(timeUs));
    checkEndOfTimeline(timeUs);
}

void PlaybackEngine::onCompositeTick()
{
    if (!m_playing || !m_project)
        return;

    m_compositor.requestComposite(m_clock.currentTimeUs(), playbackRenderOptions());
}

void PlaybackEngine::onCompositeFinished()
{
    if (!m_playing || !m_project || !isQualityMode())
        return;

    // Step forward only if the playhead is still where this frame was requested;
    // a seek that arrived while it rendered is honoured instead of skipped past.
    if (m_qualityRequestUs == m_playheadUs) {
        m_playheadUs += frameStepUs();
        emit playheadUsChanged(static_cast<quint64>(m_playheadUs));
        checkEndOfTimeline(m_playheadUs);
        if (m_playheadUs >= m_project->durationUs())
            return;
    }

    m_qualityRequestUs = m_playheadUs;
    m_compositor.requestComposite(m_playheadUs, playbackRenderOptions());
}

void PlaybackEngine::onFrameReady(const GpuFrameTexture &frame)
{
    if (!frame.isValid())
        return;

    {
        QMutexLocker lock(&m_frameMutex);
        m_currentFrame = frame;
    }
    emit currentFrameChanged();
}

FrameCompositor::RenderOptions PlaybackEngine::playbackRenderOptions() const
{
    FrameCompositor::RenderOptions options;
    if (m_previewQuality == QStringLiteral("quarter")) {
        options.previewScale = 0.25;
    } else if (m_previewQuality == QStringLiteral("half")) {
        options.previewScale = 0.5;
    } else {
        options.previewScale = 1.0;
    }
    // During fast playback, cap temporal history so time_echo cannot multiply
    // decode work unboundedly. Paused, scrubbed and quality-mode frames keep the
    // full history: those are exactly the cases where fidelity is the point.
    options.maxTimeEchoHistoryFrames = m_playing && !isQualityMode() ? 12 : -1;

    // Buffer decoded frames ahead of the playhead only while realtime playback is
    // actually running: paused and quality-mode frames have no deadline to miss,
    // and read-ahead during editing is thrown away by the next edit.
    options.readAheadUs = m_playing && !isQualityMode() ? kReadAheadUs : 0;

    if (m_project && m_previewRenderWidth > 0 && m_previewRenderHeight > 0
        && m_previewQuality == QStringLiteral("full")) {
        const double widthScale = static_cast<double>(m_previewRenderWidth) / qMax(1, m_project->width());
        const double heightScale = static_cast<double>(m_previewRenderHeight) / qMax(1, m_project->height());
        options.previewScale = qBound(0.1, qMin(widthScale, heightScale), 1.0);
    }

    // Hide the text clip being edited in place so the QML inline editor stands in
    // for it. Never applies while playing (no inline edit during playback).
    if (!m_playing)
        options.skipClipId = m_editingClipId;

    return options;
}

void PlaybackEngine::setEditingClipId(const QString &id)
{
    if (m_editingClipId == id)
        return;
    m_editingClipId = id;
    if (!m_playing)
        refreshFrame();
}

int PlaybackEngine::fillAudio(float *buffer, int sampleCount)
{
    if (!buffer || sampleCount <= 0)
        return 0;

    if (!m_playing || !m_project) {
        std::memset(buffer, 0, static_cast<size_t>(sampleCount) * 2 * sizeof(float));
        return sampleCount;
    }

    // Mix at the produce position (audio we are generating into the buffer),
    // then anchor the visible playhead to what the sink has actually played so
    // video follows audio rather than leading it by the buffer depth.
    if (qFuzzyCompare(m_playbackRate, 1.0)) {
        m_mixer.mix(m_clock.produceTimeUs(), sampleCount, m_sampleRate, buffer);
    } else {
        // Off 1x, the whole mix is treated as one source and stretched, which keeps the pitch and
        // leaves AudioMixer alone — it maps sample counts to timeline microseconds 1:1 throughout,
        // and threading a global rate through it would touch every clip overlap test in there.
        // The retimer's "timeline" here is the sink's own output, and its "source" is the project
        // timeline; it owns the read cursor, so the mixer is pulled at whatever position it wants.
        drift::ClipAudioBlock block;
        block.identity = m_audioStreamGeneration.load(std::memory_order_acquire);
        block.sampleRate = m_sampleRate;
        block.timelineStartUs = m_clock.renderedFramesUs();
        block.sourceStartUs = m_clock.produceTimeUs();
        block.tempo = m_playbackRate;

        m_rateRetimer.process(
            block,
            [this](drift::TimeUs sourceStartUs, int frames, float *dst) {
                m_mixer.mix(sourceStartUs, frames, m_sampleRate, dst);
                // The mixer is silent rather than exhausted past the end of the timeline; playback
                // stops on the playhead reaching the duration, not on the source running out.
                return frames;
            },
            sampleCount, buffer);
    }
    m_clock.onAudioSamplesRendered(sampleCount);
    if (m_sink)
        m_clock.syncPlaybackUs(static_cast<drift::TimeUs>(m_sink->processedUSecs()));
    return sampleCount;
}

#include "ClipPreviewPlayer.h"

#include "ClipPreviewImageStore.h"
#include "engine/AudioMixer.h"
#include "engine/ClipReaderPool.h"

#include <QAudioSink>

#include <cstring>

namespace {

constexpr int kPositionUpdateMs = 16; // ~60 Hz, independent of decode

// The window is a working surface, not a mastering monitor: decoding at preview size keeps the
// pump ahead of the clock even where the ramp asks for 8×.
constexpr int kFrameMaxWidth = 960;
constexpr int kFrameMaxHeight = 540;

} // namespace

ClipPreviewAudioDevice::ClipPreviewAudioDevice(ClipPreviewPlayer *player, QObject *parent)
    : QIODevice(parent)
    , m_player(player)
{
    open(QIODevice::ReadOnly);
}

qint64 ClipPreviewAudioDevice::readData(char *data, qint64 maxlen)
{
    if (!m_player || maxlen <= 0)
        return 0;

    const int maxSamples = static_cast<int>(maxlen / (sizeof(float) * 2));
    if (maxSamples <= 0)
        return 0;

    const int samples = m_player->fillAudio(reinterpret_cast<float *>(data), maxSamples);
    return static_cast<qint64>(samples) * static_cast<qint64>(sizeof(float) * 2);
}

qint64 ClipPreviewAudioDevice::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);
    return -1;
}

qint64 ClipPreviewAudioDevice::bytesAvailable() const
{
    return (1 << 20) + QIODevice::bytesAvailable();
}

void ClipPreviewFrameWorker::decode(const QString &path, qint64 sourceUs, int maxWidth, int maxHeight,
                                    quint64 token)
{
    if (token < latestToken.load(std::memory_order_acquire))
        return; // a newer position arrived while this sat in the queue

    const QImage image = ClipReaderPool::instance().readVideoFrame(
        path, static_cast<drift::TimeUs>(sourceUs), maxWidth, maxHeight);
    if (!image.isNull())
        emit decoded(image, token);
}

ClipPreviewPlayer::ClipPreviewPlayer(QObject *parent)
    : QObject(parent)
    , m_device(new ClipPreviewAudioDevice(this))
    , m_frameWorker(new ClipPreviewFrameWorker)
{
    m_device->moveToThread(&m_audioThread);
    m_audioThread.setObjectName(QStringLiteral("ClipPreviewAudio"));
    m_audioThread.start();

    m_frameWorker->moveToThread(&m_frameThread);
    m_frameThread.setObjectName(QStringLiteral("ClipPreviewDecode"));
    connect(&m_frameThread, &QThread::finished, m_frameWorker, &QObject::deleteLater);
    connect(m_frameWorker, &ClipPreviewFrameWorker::decoded, this, &ClipPreviewPlayer::onDecoded);
    m_frameThread.start();

    m_positionTimer.setTimerType(Qt::PreciseTimer);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_positionTimer, &QTimer::timeout, this, &ClipPreviewPlayer::onPositionTick);
    connect(&m_frameTimer, &QTimer::timeout, this, [this] { requestFrame(positionUs()); });
}

ClipPreviewPlayer::~ClipPreviewPlayer()
{
    m_playing = false;
    m_positionTimer.stop();
    m_frameTimer.stop();
    m_clock.stop();

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

    m_frameThread.quit();
    m_frameThread.wait();

    delete m_device;
    m_device = nullptr;
}

void ClipPreviewPlayer::ensureAudioSink()
{
    QMetaObject::invokeMethod(
        m_device,
        [this] {
            m_format.setSampleRate(m_sampleRate);
            m_format.setChannelCount(2);
            m_format.setSampleFormat(QAudioFormat::Float);
            if (!m_sink)
                m_sink = new QAudioSink(m_format, m_device);
        },
        Qt::BlockingQueuedConnection);
}

void ClipPreviewPlayer::setClip(const drift::Clip &clip, int sampleRate, int fps)
{
    pause();

    m_sampleRate = qMax(8000, sampleRate);
    m_fps = qMax(1, fps);

    {
        QMutexLocker lock(&m_clipMutex);
        m_clip = clip;
        m_clip.timelineStart = 0;
        m_clip.syncDurationFromSpeedCurve();
    }

    m_positionUs = 0;
    m_frameSize = QSize();
    ClipPreviewImageStore::clear();

    emit durationChanged();
    emit positionChanged();
    requestFrame(0);
}

void ClipPreviewPlayer::setSpeedCurve(const drift::SpeedCurve &curve)
{
    drift::TimeUs duration = 0;
    {
        QMutexLocker lock(&m_clipMutex);
        m_clip.speedCurve = curve;
        m_clip.syncDurationFromSpeedCurve();
        duration = m_clip.timelineDuration;
    }

    // The ramp moved under the playhead; keep it inside the clip it now describes.
    if (m_positionUs > duration)
        seek(duration);

    emit durationChanged();
    if (!m_playing)
        requestFrame(m_positionUs);
}

void ClipPreviewPlayer::clear()
{
    pause();
    {
        QMutexLocker lock(&m_clipMutex);
        m_clip = drift::Clip{};
    }
    m_positionUs = 0;
    m_frameSize = QSize();
    ClipPreviewImageStore::clear();
    emit frameChanged();
    emit durationChanged();
    emit positionChanged();
}

drift::TimeUs ClipPreviewPlayer::durationUs() const
{
    QMutexLocker lock(&m_clipMutex);
    return m_clip.timelineDuration;
}

drift::TimeUs ClipPreviewPlayer::positionUs() const
{
    return m_playing ? m_clock.currentTimeUs() : m_positionUs;
}

QSize ClipPreviewPlayer::frameSize() const
{
    return m_frameSize;
}

void ClipPreviewPlayer::play()
{
    if (m_playing || durationUs() <= 0)
        return;

    // Restart from the top rather than sitting stuck on the tail.
    if (m_positionUs >= durationUs())
        m_positionUs = 0;

    ensureAudioSink();
    m_clock.reset(m_positionUs, m_sampleRate);
    m_clock.start();
    m_playing = true;

    QMetaObject::invokeMethod(
        m_device,
        [this] {
            if (m_sink)
                m_sink->start(m_device);
        },
        Qt::BlockingQueuedConnection);

    emit playingChanged();

    m_positionTimer.start(kPositionUpdateMs);
    m_frameTimer.start(qMax(1, 1000 / m_fps));
}

void ClipPreviewPlayer::pause()
{
    if (!m_playing)
        return;

    m_playing = false;
    m_positionTimer.stop();
    m_frameTimer.stop();
    m_clock.pause();
    m_positionUs = m_clock.pausedAt();

    QMetaObject::invokeMethod(
        m_device,
        [this] {
            if (m_sink)
                m_sink->stop();
        },
        Qt::BlockingQueuedConnection);

    emit playingChanged();
    emit positionChanged();
}

void ClipPreviewPlayer::seek(drift::TimeUs us)
{
    m_positionUs = qBound<drift::TimeUs>(0, us, durationUs());
    if (m_playing) {
        m_clock.reset(m_positionUs, m_sampleRate);
        m_clock.start();
    }
    emit positionChanged();
    requestFrame(m_positionUs);
}

void ClipPreviewPlayer::onPositionTick()
{
    if (!m_playing)
        return;

    const drift::TimeUs duration = durationUs();
    const drift::TimeUs now = m_clock.currentTimeUs();
    if (now >= duration) {
        m_positionUs = duration;
        pause();
        requestFrame(m_positionUs);
        return;
    }

    m_positionUs = now;
    emit positionChanged();
}

void ClipPreviewPlayer::requestFrame(drift::TimeUs position)
{
    QString path;
    drift::TimeUs sourceUs = 0;
    {
        QMutexLocker lock(&m_clipMutex);
        if (m_clip.type == drift::ClipType::Audio || m_clip.path.isEmpty())
            return;
        path = m_clip.path;
        sourceUs = m_clip.timelineToSourceUs(position);
    }

    const quint64 token = ++m_frameToken;
    m_frameWorker->latestToken.store(token, std::memory_order_release);
    QMetaObject::invokeMethod(m_frameWorker, "decode", Qt::QueuedConnection, Q_ARG(QString, path),
                              Q_ARG(qint64, static_cast<qint64>(sourceUs)), Q_ARG(int, kFrameMaxWidth),
                              Q_ARG(int, kFrameMaxHeight), Q_ARG(quint64, token));
}

void ClipPreviewPlayer::onDecoded(const QImage &image, quint64 token)
{
    // Decodes can finish out of order across seeks; only ever move forward.
    if (token < m_shownToken)
        return;
    m_shownToken = token;

    ClipPreviewImageStore::setFrame(image);
    if (m_frameSize != image.size()) {
        m_frameSize = image.size();
        emit frameSizeChanged();
    }
    emit frameChanged();
}

int ClipPreviewPlayer::fillAudio(float *buffer, int sampleCount)
{
    if (!buffer || sampleCount <= 0)
        return 0;

    if (!m_playing) {
        std::memset(buffer, 0, static_cast<size_t>(sampleCount) * 2 * sizeof(float));
        return sampleCount;
    }

    drift::Clip clip;
    {
        QMutexLocker lock(&m_clipMutex);
        clip = m_clip;
    }

    const drift::TimeUs timeUs = m_clock.produceTimeUs();
    const QVector<float> mixed = AudioMixer::readClipAudio(clip, timeUs, sampleCount, m_sampleRate);
    const int frames = qMin(sampleCount, static_cast<int>(mixed.size() / 2));
    std::memcpy(buffer, mixed.constData(), static_cast<size_t>(frames) * 2 * sizeof(float));
    if (frames < sampleCount) {
        std::memset(buffer + static_cast<size_t>(frames) * 2, 0,
                    static_cast<size_t>(sampleCount - frames) * 2 * sizeof(float));
    }

    m_clock.onAudioSamplesRendered(sampleCount);
    if (m_sink)
        m_clock.syncPlaybackUs(static_cast<drift::TimeUs>(m_sink->processedUSecs()));
    return sampleCount;
}

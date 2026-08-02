#pragma once

#include "CompositorService.h"
#include "PlaybackClock.h"
#include "core/Project.h"
#include "core/Time.h"
#include "engine/AudioMixer.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <atomic>

class PlaybackEngine;

// Pull-mode audio device; rendered samples advance the audio-master clock.
class AudioPlaybackIODevice : public QIODevice
{
public:
    explicit AudioPlaybackIODevice(PlaybackEngine *engine, QObject *parent = nullptr);

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;
    // Endless generated source: always advertise data so the sink keeps pulling.
    qint64 bytesAvailable() const override;
    bool isSequential() const override { return true; }

private:
    PlaybackEngine *m_engine = nullptr;
};

// Audio-master playback: mixes timeline audio and composites preview frames off the GUI thread.
class PlaybackEngine : public QObject
{
    Q_OBJECT

    // The composited frame lives in GPU memory; the preview item wraps this
    // texture directly rather than uploading a QImage every frame.
    Q_PROPERTY(int previewTextureId READ previewTextureId NOTIFY currentFrameChanged)
    Q_PROPERTY(QSize previewTextureSize READ previewTextureSize NOTIFY currentFrameChanged)
    Q_PROPERTY(QImage previewImage READ previewImage NOTIFY currentFrameChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(QString previewQuality READ previewQuality WRITE setPreviewQuality NOTIFY previewQualityChanged)
    Q_PROPERTY(QString playbackMode READ playbackMode WRITE setPlaybackMode NOTIFY playbackModeChanged)

public:
    explicit PlaybackEngine(QObject *parent = nullptr);
    ~PlaybackEngine() override;

    void setProject(drift::Project *project);
    void setPlayheadUs(drift::TimeUs us);
    drift::TimeUs playheadUs() const { return m_playheadUs; }

    int previewTextureId() const;
    QSize previewTextureSize() const;
    QImage previewImage() const;
    bool hasFrame() const;
    bool isPlaying() const { return m_playing; }
    QString previewQuality() const;
    void setPreviewQuality(const QString &quality);
    // "fast": realtime playback with audio, late frames dropped. "quality":
    // every frame is rendered and shown, silently and slower than realtime.
    QString playbackMode() const;
    void setPlaybackMode(const QString &mode);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void refreshFrame();
    Q_INVOKABLE void setPreviewRenderSize(int width, int height);

    // Id of the text clip currently edited in place on the preview; that clip is
    // omitted from the composited frame so the QML inline editor stands in for it.
    void setEditingClipId(const QString &id);

signals:
    void currentFrameChanged();
    void playingChanged();
    void previewQualityChanged();
    void playbackModeChanged();
    void playheadUsChanged(quint64 us);

private:
    friend class AudioPlaybackIODevice;

    int fillAudio(float *buffer, int sampleCount);
    void ensureAudioSink();
    void onPlayheadTick();
    void onCompositeTick();
    void onCompositeFinished();
    void onFrameReady(const GpuFrameTexture &frame);
    void checkEndOfTimeline(drift::TimeUs timeUs);
    bool isQualityMode() const { return m_playbackMode == QStringLiteral("quality"); }
    drift::TimeUs frameStepUs() const;
    FrameCompositor::RenderOptions playbackRenderOptions() const;

    drift::Project *m_project = nullptr;
    PlaybackClock m_clock;
    CompositorService m_compositor;
    AudioMixer m_mixer;
    QAudioFormat m_format;
    // The sink and its pull device live on m_audioThread so that ring-buffer
    // refills (which synchronously decode audio) never block the GUI thread.
    QThread m_audioThread;
    QAudioSink *m_sink = nullptr;
    AudioPlaybackIODevice *m_device = nullptr;
    QTimer m_playheadTimer;
    QTimer m_compositeTimer;
    GpuFrameTexture m_currentFrame;
    mutable QMutex m_frameMutex;
    drift::TimeUs m_playheadUs = 0;
    std::atomic<bool> m_playing = false;
    QString m_previewQuality = QStringLiteral("full");
    QString m_playbackMode = QStringLiteral("fast");
    // Playhead position the in-flight quality-mode frame was requested for; a
    // seek that lands elsewhere while it renders must not be stepped over.
    drift::TimeUs m_qualityRequestUs = -1;
    QString m_editingClipId;
    int m_previewRenderWidth = 0;
    int m_previewRenderHeight = 0;
    int m_sampleRate = 48000;
};

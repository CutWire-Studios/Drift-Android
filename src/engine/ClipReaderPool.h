#pragma once

#include "ClipReader.h"
#include "core/Time.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThread>

#include <QList>

#include <atomic>
#include <map>
#include <memory>

class ClipReaderWorker;

// Threaded reader pool: one worker thread per media path (video and audio are separate).
class ClipReaderPool
{
public:
    static ClipReaderPool &instance();

    struct VideoRequest
    {
        QString path;
        drift::TimeUs sourceUs = 0;
        int maxWidth = 0;
        int maxHeight = 0;
    };

    // Kick every request off on its own worker thread without waiting. Each path
    // has its own thread, so the decodes run concurrently; the readVideoFrame
    // calls that follow then hit each reader's cache instead of decoding one
    // clip after another on the caller's thread.
    void warmVideoFrames(const QList<VideoRequest> &requests);

    // How far past the frame being composited each reader keeps decoding, in
    // source time. Set per composite from the render options; 0 (the default)
    // leaves the plain one-frame-ahead prefetch. Only the NV12 preview path
    // buffers — export and thumbnails consume as fast as they decode anyway.
    void setReadAheadUs(drift::TimeUs readAheadUs);

    QImage readVideoFrame(const QString &path, drift::TimeUs sourceUs, int maxWidth, int maxHeight);
    // Preview path: NV12 for cheaper GPU upload. Falls back empty when decode fails.
    Nv12Frame readVideoFrameNv12(const QString &path, drift::TimeUs sourceUs, int maxWidth, int maxHeight);
    int readAudioInterleaved(const QString &path, drift::TimeUs sourceStartUs, int sampleCount,
                             int outputSampleRate, float *interleavedStereoOut);
    void retainActivePaths(const QSet<QString> &videoPaths, const QSet<QString> &audioPaths);

private:
    ClipReaderPool() = default;
    ~ClipReaderPool();

    struct WorkerEntry
    {
        std::unique_ptr<QThread> thread;
        ClipReaderWorker *worker = nullptr;
    };

    static void stopWorkerEntry(WorkerEntry &entry);
    ClipReaderWorker *ensureWorker(std::map<QString, std::unique_ptr<WorkerEntry>> &workers, const QString &path);

    QMutex m_mutex;
    std::atomic<drift::TimeUs> m_readAheadUs{0};
    std::map<QString, std::unique_ptr<WorkerEntry>> m_videoWorkers;
    std::map<QString, std::unique_ptr<WorkerEntry>> m_audioWorkers;
};

#pragma once

#include "ClipReader.h"

#include "core/Time.h"

#include <QAtomicInt>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>

// Owns a ClipReader on a dedicated thread; all decode calls are serialized here.
// The reader keeps its own frame cache, so this class holds no cache of its own.
class ClipReaderWorker : public QObject
{
    Q_OBJECT

public:
    explicit ClipReaderWorker(QObject *parent = nullptr);

    // Callable from any thread. Queues one read-ahead step if none is pending;
    // that step re-arms itself until the reader has readAheadUs of decoded source
    // buffered. Keeping a single step in flight is what bounds a decode request's
    // wait to one frame — a queue of them would serialize ahead of it.
    void requestPrefetchNv12(int maxWidth, int maxHeight, drift::TimeUs readAheadUs);

public slots:
    void openPath(const QString &path);
    void closePath();
    QImage decodeVideo(drift::TimeUs sourceUs, int maxWidth, int maxHeight);
    Nv12Frame decodeVideoNv12(drift::TimeUs sourceUs, int maxWidth, int maxHeight);
    int decodeAudio(drift::TimeUs sourceStartUs, int sampleCount, int outputSampleRate,
                    float *interleavedStereoOut);
    void prefetchNextVideo(int maxWidth, int maxHeight);
    void prefetchNextVideoNv12(int maxWidth, int maxHeight, drift::TimeUs readAheadUs);

private:
    ClipReader m_reader;
    QMutex m_mutex;
    QAtomicInt m_prefetchPending{0};
};

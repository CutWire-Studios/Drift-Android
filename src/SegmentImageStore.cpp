#include "SegmentImageStore.h"

#include <QMutex>

namespace {

QMutex g_mutex;
QImage g_frame;
QImage g_mask;

} // namespace

namespace SegmentImageStore {

void setFrame(const QImage &frame)
{
    QMutexLocker lock(&g_mutex);
    g_frame = frame;
}

void setMask(const QImage &mask)
{
    QMutexLocker lock(&g_mutex);
    g_mask = mask;
}

void clear()
{
    QMutexLocker lock(&g_mutex);
    g_frame = QImage();
    g_mask = QImage();
}

QImage frame()
{
    QMutexLocker lock(&g_mutex);
    return g_frame;
}

QImage mask()
{
    QMutexLocker lock(&g_mutex);
    return g_mask;
}

} // namespace SegmentImageStore

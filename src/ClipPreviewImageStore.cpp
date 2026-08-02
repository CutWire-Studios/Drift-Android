#include "ClipPreviewImageStore.h"

#include <QMutex>

namespace {

QMutex g_mutex;
QImage g_frame;

} // namespace

namespace ClipPreviewImageStore {

void setFrame(const QImage &frame)
{
    QMutexLocker lock(&g_mutex);
    g_frame = frame;
}

void clear()
{
    QMutexLocker lock(&g_mutex);
    g_frame = QImage();
}

QImage frame()
{
    QMutexLocker lock(&g_mutex);
    return g_frame;
}

} // namespace ClipPreviewImageStore

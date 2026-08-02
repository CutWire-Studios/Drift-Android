#include "FilmstripTileCache.h"

#include "MediaThumbnail.h"

#include <QFileInfo>
#include <QThreadPool>
#include <QTimer>

namespace {

// Deep enough to cover a screenful of tiles a few times over; beyond that the oldest
// requests are stale scroll history and get dropped.
constexpr int kMaxQueued = 256;
// One decoder pass per batch. Small enough that a zoom change is served promptly,
// large enough that the seeks in it stay close together in the file.
constexpr int kBatchSize = 24;
constexpr qint64 kTileCacheMaxBytes = 128LL * 1024 * 1024;
constexpr int kPruneEvery = 512;

} // namespace

FilmstripTileCache::FilmstripTileCache(QObject *parent)
    : QObject(parent)
{
}

QString FilmstripTileCache::keyFor(const QString &sourcePath, int level, qint64 index)
{
    return sourcePath + QLatin1Char('|') + QString::number(level) + QLatin1Char('|')
           + QString::number(index);
}

QString FilmstripTileCache::tile(const QString &sourcePath, int level, qint64 index)
{
    if (sourcePath.isEmpty() || m_failed.contains(sourcePath))
        return {};

    const QString key = keyFor(sourcePath, level, index);
    const auto cached = m_ready.constFind(key);
    if (cached != m_ready.constEnd())
        return cached.value();

    if (m_queued.contains(key))
        return {};

    // A tile written by an earlier session is on disk but not in m_ready yet.
    const QString path = MediaThumbnail::tilePath(sourcePath, level, index);
    if (QFileInfo::exists(path)) {
        m_ready.insert(key, path);
        return path;
    }

    m_queued.insert(key);
    m_queue.append({sourcePath, level, index});
    while (m_queue.size() > kMaxQueued) {
        m_queued.remove(keyFor(m_queue.first().sourcePath, m_queue.first().level,
                               m_queue.first().index));
        m_queue.removeFirst();
    }

    scheduleBatch();
    return {};
}

void FilmstripTileCache::scheduleBatch()
{
    if (m_busy || m_batchScheduled || m_queue.isEmpty())
        return;

    // Let the whole burst of requests from one layout pass land before picking a batch,
    // so the batch reflects the final viewport rather than the first tile that asked.
    m_batchScheduled = true;
    QTimer::singleShot(0, this, &FilmstripTileCache::runBatch);
}

void FilmstripTileCache::runBatch()
{
    m_batchScheduled = false;
    if (m_busy || m_queue.isEmpty())
        return;

    // Newest first: those are the tiles the viewport asked for most recently.
    const Request newest = m_queue.last();
    QList<qint64> indices;
    for (int i = m_queue.size() - 1; i >= 0 && indices.size() < kBatchSize; --i) {
        const Request &request = m_queue.at(i);
        if (request.sourcePath != newest.sourcePath || request.level != newest.level)
            continue;
        indices.append(request.index);
        m_queue.removeAt(i);
    }
    if (indices.isEmpty())
        return;

    m_busy = true;
    const QString sourcePath = newest.sourcePath;
    const int level = newest.level;
    QThreadPool::globalInstance()->start([this, sourcePath, level, indices] {
        const QList<qint64> produced = MediaThumbnail::generateTiles(sourcePath, level, indices);
        QMetaObject::invokeMethod(
            this,
            [this, sourcePath, level, produced, indices] {
                applyBatch(sourcePath, level, produced, indices);
            },
            Qt::QueuedConnection);
    });
}

void FilmstripTileCache::applyBatch(const QString &sourcePath, int level,
                                    const QList<qint64> &produced, const QList<qint64> &requested)
{
    m_busy = false;

    for (const qint64 index : requested)
        m_queued.remove(keyFor(sourcePath, level, index));

    for (const qint64 index : produced)
        m_ready.insert(keyFor(sourcePath, level, index),
                       MediaThumbnail::tilePath(sourcePath, level, index));

    if (produced.isEmpty()) {
        // Repeatedly empty means no video stream, or the file is gone. Stop asking; a single
        // empty batch can just be seeks landing past the end of a short source.
        if (++m_emptyBatches[sourcePath] >= 3)
            m_failed.insert(sourcePath);
    } else {
        m_emptyBatches.remove(sourcePath);
        emit tileReady(sourcePath);
    }

    m_producedSincePrune += produced.size();
    if (m_producedSincePrune >= kPruneEvery) {
        m_producedSincePrune = 0;
        QThreadPool::globalInstance()->start([this] {
            MediaThumbnail::pruneTileCache(kTileCacheMaxBytes);
            // Pruning may have deleted tiles we have memoized; drop the lot and let the next
            // lookup re-check the disk rather than hand out paths that no longer exist.
            QMetaObject::invokeMethod(this, [this] { m_ready.clear(); }, Qt::QueuedConnection);
        });
    }

    scheduleBatch();
}

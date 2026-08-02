#include "WaveformBlockCache.h"

#include "MediaWaveform.h"

#include <QThreadPool>
#include <QTimer>

#include <cmath>

namespace {

// One batch is a single contiguous decode, so this is how much audio a job chews through
// before the timeline gets to draw it. Eight minutes fills a viewport in one pass while
// still landing often enough to look progressive.
constexpr int kBatchBlocks = 16;
// Deeper than a viewport needs, so panning back and forth doesn't lose queued work; older
// entries beyond this are scroll history nobody is looking at any more.
constexpr int kMaxQueued = 512;
// Output slots per query. A canvas is viewport-sized, so this is only ever a ceiling.
constexpr int kMaxOutCount = 8192;

} // namespace

WaveformBlockCache::WaveformBlockCache(QObject *parent)
    : QObject(parent)
{
}

QString WaveformBlockCache::keyFor(const QString &sourcePath, int block)
{
    return sourcePath + QLatin1Char('|') + QString::number(block);
}

QVector<float> WaveformBlockCache::range(const QString &sourcePath, double startSeconds,
                                         double durSeconds, int outCount)
{
    if (sourcePath.isEmpty() || durSeconds <= 0.0 || outCount <= 0)
        return {};

    outCount = qMin(outCount, kMaxOutCount);
    QVector<float> out(outCount, -1.0f);

    const double start = qMax(0.0, startSeconds);
    const double end = start + durSeconds;
    const qint64 firstPeak = static_cast<qint64>(std::floor(start * kPeaksPerSecond));
    const qint64 lastPeak = static_cast<qint64>(std::ceil(end * kPeaksPerSecond)) - 1;
    if (lastPeak < firstPeak)
        return {};

    const int firstBlock = static_cast<int>(firstPeak / kPeaksPerBlock);
    const int lastBlock = static_cast<int>(lastPeak / kPeaksPerBlock);
    bool queuedAny = false;

    for (int b = firstBlock; b <= lastBlock; ++b) {
        const QString key = keyFor(sourcePath, b);
        if (m_blocks.contains(key) || m_queued.contains(key))
            continue;
        m_queued.insert(key);
        m_queue.append({sourcePath, b});
        queuedAny = true;
    }

    // Gather per output slot rather than scattering per peak, so zooming in past one peak
    // per pixel repeats the nearest peak instead of leaving gaps between hairlines. Slots
    // advance monotonically, so the block lookup only changes once per block.
    const qint64 spanPeaks = lastPeak - firstPeak + 1;
    const QVector<float> *block = nullptr;
    int blockIndex = -1;
    auto peakAt = [&](qint64 g) -> float {
        const int b = static_cast<int>(g / kPeaksPerBlock);
        if (b != blockIndex) {
            blockIndex = b;
            const auto it = m_blocks.constFind(keyFor(sourcePath, b));
            block = it == m_blocks.constEnd() ? nullptr : &it.value();
        }
        if (!block)
            return -1.0f;
        const int i = static_cast<int>(g - static_cast<qint64>(b) * kPeaksPerBlock);
        return i >= 0 && i < block->size() ? (*block)[i] : -1.0f;
    };

    for (int j = 0; j < outCount; ++j) {
        const qint64 g0 = firstPeak
                          + static_cast<qint64>(static_cast<double>(j) * spanPeaks / outCount);
        qint64 g1 = firstPeak
                    + static_cast<qint64>(static_cast<double>(j + 1) * spanPeaks / outCount);
        if (g1 <= g0)
            g1 = g0 + 1;
        float peak = -1.0f;
        for (qint64 g = g0; g < g1; ++g)
            peak = qMax(peak, peakAt(g));
        out[j] = peak;
    }

    if (queuedAny) {
        while (m_queue.size() > kMaxQueued) {
            m_queued.remove(keyFor(m_queue.first().first, m_queue.first().second));
            m_queue.removeFirst();
        }
        scheduleBatch();
    }

    return out;
}

void WaveformBlockCache::scheduleBatch()
{
    if (m_busy || m_batchScheduled || m_queue.isEmpty())
        return;

    // Let the whole burst of requests from one layout pass land first, so the batch reflects
    // the settled viewport rather than the first block that asked.
    m_batchScheduled = true;
    QTimer::singleShot(0, this, &WaveformBlockCache::runBatch);
}

void WaveformBlockCache::runBatch()
{
    m_batchScheduled = false;
    if (m_busy || m_queue.isEmpty())
        return;

    // Newest first: that is what the viewport asked for most recently. Take a contiguous run
    // from there so the decode is one seek followed by a straight read.
    const QString sourcePath = m_queue.last().first;
    const int anchor = m_queue.last().second;

    QSet<int> wanted;
    for (int i = m_queue.size() - 1; i >= 0 && wanted.size() < kBatchBlocks; --i) {
        if (m_queue.at(i).first != sourcePath)
            continue;
        const int block = m_queue.at(i).second;
        if (block < anchor - kBatchBlocks || block > anchor + kBatchBlocks)
            continue;
        wanted.insert(block);
    }
    if (wanted.isEmpty())
        return;

    // Grow outward from the anchor while blocks are adjacent, so gaps aren't decoded through.
    int first = anchor;
    int last = anchor;
    while (wanted.contains(first - 1))
        --first;
    while (wanted.contains(last + 1))
        ++last;

    QList<int> requested;
    for (int block = first; block <= last; ++block) {
        requested.append(block);
        m_queued.remove(keyFor(sourcePath, block));
    }
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (m_queue.at(i).first == sourcePath && m_queue.at(i).second >= first
            && m_queue.at(i).second <= last)
            m_queue.removeAt(i);
    }

    m_busy = true;
    const double startSeconds = static_cast<double>(first) * kBlockSeconds;
    const double endSeconds = static_cast<double>(last + 1) * kBlockSeconds;
    QThreadPool::globalInstance()->start([this, sourcePath, first, startSeconds, endSeconds,
                                          requested] {
        const QVector<float> peaks =
            MediaWaveform::peaksForRange(sourcePath, startSeconds, endSeconds, kPeaksPerSecond);

        // Split the one decode back into per-block entries so panning reuses them piecemeal.
        QList<QVector<float>> blocks;
        for (int i = 0; i < requested.size(); ++i) {
            const int from = i * kPeaksPerBlock;
            if (from >= peaks.size())
                break;
            blocks.append(peaks.mid(from, kPeaksPerBlock));
        }

        QMetaObject::invokeMethod(
            this,
            [this, sourcePath, first, blocks, requested] {
                applyBatch(sourcePath, first, blocks, requested);
            },
            Qt::QueuedConnection);
    });
}

void WaveformBlockCache::applyBatch(const QString &sourcePath, int firstBlock,
                                    const QList<QVector<float>> &blocks, const QList<int> &requested)
{
    m_busy = false;

    for (int i = 0; i < blocks.size(); ++i)
        m_blocks.insert(keyFor(sourcePath, firstBlock + i), blocks.at(i));

    // Blocks the decode never reached are past the end of the media (or unreadable). Store
    // them empty so they read as "decoded, nothing there" instead of being asked for forever.
    for (const int block : requested) {
        const QString key = keyFor(sourcePath, block);
        if (!m_blocks.contains(key))
            m_blocks.insert(key, {});
    }

    if (!blocks.isEmpty())
        emit rangeReady(sourcePath);

    scheduleBatch();
}

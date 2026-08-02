#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

// On-demand filmstrip tiles for the timeline.
//
// Asking for a tile is a lookup: it returns the cached file straight away, or an empty string
// and queues a decode. Only tiles the timeline actually instantiates are ever requested, so a
// three-hour clip costs a screenful of frames rather than a full-length strip.
//
// The queue is served newest-first and capped, because scrolling a long timeline asks for far
// more tiles than it keeps on screen — the ones that scrolled away fall off the back rather
// than blocking the ones under the cursor.
class FilmstripTileCache : public QObject
{
    Q_OBJECT

public:
    explicit FilmstripTileCache(QObject *parent = nullptr);

    // Cached tile file for `2^level`-second tile number `index` of `sourcePath`, or an empty
    // string if it still needs decoding — tileReady() fires for the source when it lands.
    QString tile(const QString &sourcePath, int level, qint64 index);

signals:
    void tileReady(const QString &sourcePath);

private:
    struct Request
    {
        QString sourcePath;
        int level = 0;
        qint64 index = 0;
    };

    void scheduleBatch();
    void runBatch();
    void applyBatch(const QString &sourcePath, int level, const QList<qint64> &produced,
                    const QList<qint64> &requested);

    static QString keyFor(const QString &sourcePath, int level, qint64 index);

    QHash<QString, QString> m_ready;
    // Sources whose decode failed, so a broken or audio-only file isn't retried every repaint.
    QSet<QString> m_failed;
    QHash<QString, int> m_emptyBatches;
    QSet<QString> m_queued;
    QList<Request> m_queue;
    bool m_busy = false;
    bool m_batchScheduled = false;
    int m_producedSincePrune = 0;
};

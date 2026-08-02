#pragma once

#include <QList>
#include <QString>

class MediaThumbnail
{
public:
    static constexpr int kFilmstripFrameWidth = 120;
    static constexpr int kFilmstripFrameHeight = 68;
    static constexpr int kFilmstripFrameCount = 8;

    static QString generate(const QString &sourcePath, const QString &kind);
    static QString generateFilmstrip(const QString &sourcePath, const QString &kind);
    static QString generateAtTime(const QString &sourcePath, double sourceSeconds);

    // On-demand filmstrip tiles. The coarse strip above only ever holds 8 frames, so a long
    // clip repeats the same image for thousands of px; these fill in the real frame for a
    // given moment. A tile covers `2^level` source seconds starting at `index * 2^level`,
    // so zooming picks a finer level and panning reuses everything already cached.
    static QString tilePath(const QString &sourcePath, int level, qint64 index);

    // Decodes the missing tiles among `indices` with one open decoder, in ascending time
    // order so seeking stays near-sequential. Returns the indices that are now on disk.
    static QList<qint64> generateTiles(const QString &sourcePath, int level,
                                       const QList<qint64> &indices);

    // Drops the oldest tile files until the tile cache fits in `maxBytes`.
    static void pruneTileCache(qint64 maxBytes);
};

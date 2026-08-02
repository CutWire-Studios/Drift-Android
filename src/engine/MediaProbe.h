#pragma once

#include <QList>
#include <QString>
#include <cstdint>

// Metadata for a single stream inside a probed media file.
struct StreamInfo {
    enum class Type { Video, Audio, Subtitle, Other };

    Type type = Type::Other;
    QString codecName;
    int64_t durationUs = 0;

    // Video-only fields.
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int rotationDegrees = 0;

    // True for cover-art / thumbnail streams (e.g. embedded album art in
    // MP3/FLAC). These are exposed as video streams by libav but must not be
    // treated as real video.
    bool attachedPicture = false;

    // Audio-only fields.
    int sampleRate = 0;
    int channels = 0;
};

// Result of probing a media file with libavformat.
struct MediaInfo {
    bool ok = false;
    QString errorString;
    QString path;
    int64_t durationUs = 0;
    QList<StreamInfo> streams;
};

// Thin wrapper around avformat_open_input for reading container/stream
// metadata without decoding any frames. All FFmpeg usage for probing lives
// here so the rest of the app never touches libav directly.
class MediaProbe
{
public:
    static MediaInfo probe(const QString &path);
};

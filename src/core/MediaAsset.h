#pragma once

#include "Time.h"

#include <QString>

namespace drift {

enum class MediaKind { Video, Audio, Image, Other };

QString mediaKindToString(MediaKind kind);
MediaKind mediaKindFromString(const QString &kind);

// Referenced media file with probed metadata.
struct MediaAsset
{
    QString id;
    QString path;
    // Android only: the content:// URI the media was imported from. `path` is a copy this app
    // made of it, and app storage is not forever — a device running low deletes app data, and
    // the user can wipe it from Settings — so the URI is kept as the way back to the bytes.
    // Empty on desktop, where `path` is the user's own file.
    QString sourceUri;
    QString name;
    MediaKind kind = MediaKind::Other;
    TimeUs durationUs = 0;

    int width = 0;
    int height = 0;
    double fps = 0.0;
    int rotationDegrees = 0;

    int sampleRate = 0;
    int channels = 0;
    QString codecName;

    // Explicit audio presence from probe. When false, fall back to channels/sampleRate
    // or a one-shot background probe — never sync MediaProbe from UI getters.
    bool hasAudio = false;
    bool hasAudioKnown = false;

    QString durationLabel;
    QString thumbnailPath;
    QString filmstripPath;
};

} // namespace drift

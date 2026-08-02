#include "MediaAsset.h"

namespace drift {

QString mediaKindToString(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Video:
        return QStringLiteral("video");
    case MediaKind::Audio:
        return QStringLiteral("audio");
    case MediaKind::Image:
        return QStringLiteral("image");
    case MediaKind::Other:
        break;
    }
    return QStringLiteral("other");
}

MediaKind mediaKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("video"))
        return MediaKind::Video;
    if (kind == QStringLiteral("audio"))
        return MediaKind::Audio;
    if (kind == QStringLiteral("image"))
        return MediaKind::Image;
    return MediaKind::Other;
}

} // namespace drift

#include "DriftImageProvider.h"

#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>

DriftImageProvider::DriftImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage DriftImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // Optional "?frame=<i>&count=<n>" selects one frame of a filmstrip so each tile can
    // request a distinct sub-image (a shared source URL would collapse to a single cached
    // frame). The path itself is percent-encoded, so a literal '?' only ever starts the query.
    int frame = -1;
    int frameCount = 0;
    QString encodedPath = id;
    const int queryStart = id.indexOf(QLatin1Char('?'));
    if (queryStart >= 0) {
        encodedPath = id.left(queryStart);
        const QUrlQuery query(id.mid(queryStart + 1));
        frame = query.queryItemValue(QStringLiteral("frame")).toInt();
        frameCount = query.queryItemValue(QStringLiteral("count")).toInt();
    }

    const QString path = QUrl::fromPercentEncoding(encodedPath.toUtf8());
    if (path.isEmpty()) {
        if (size)
            *size = QSize();
        return {};
    }

    const bool isResource = path.startsWith(QStringLiteral("qrc:")) || path.startsWith(QStringLiteral(":/"));
    if (!isResource && !QFileInfo::exists(path)) {
        if (size)
            *size = QSize();
        return {};
    }

    QImage image(path);
    if (image.isNull()) {
        if (size)
            *size = QSize();
        return {};
    }

    // Crop out the requested filmstrip frame before any rescale, so the returned image is a
    // single frame at its native resolution.
    if (frameCount > 0 && frame >= 0 && image.width() >= frameCount) {
        const int frameW = image.width() / frameCount;
        const int x = qMin(frame, frameCount - 1) * frameW;
        image = image.copy(QRect(x, 0, frameW, image.height()));
    }

    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }

    // Always upload as premultiplied ARGB32. Soft-alpha thumbs otherwise blend to mud,
    // and on Android/ANGLE RGB JPEGs (filmstrip tiles) have been seen to paint only while
    // the scene is continuously dirty (e.g. during pinch-zoom) then vanish when idle.
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    if (size)
        *size = image.size();
    return image;
}

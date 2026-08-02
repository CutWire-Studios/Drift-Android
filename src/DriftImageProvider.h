#pragma once

#include <QQuickImageProvider>

// Loads cached JPEG/PNG files for QML Image via image://drift/<percent-encoded-path>
class DriftImageProvider : public QQuickImageProvider
{
public:
    DriftImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

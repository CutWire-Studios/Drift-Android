#include "MaskApplier.h"

#include "core/ShapePath.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {

// Square box of the given radius, centred — masks size their parametric shapes uniformly.
QRectF squareBounds(const QPointF &center, double radius)
{
    return QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
}

QPainterPath maskPath(const drift::Mask &mask, int canvasWidth, int canvasHeight)
{
    const QPointF center(mask.x * canvasWidth, mask.y * canvasHeight);
    const double halfW = qMax(1.0, mask.w * canvasWidth * 0.5);
    const double halfH = qMax(1.0, mask.h * canvasHeight * 0.5);

    switch (mask.shape) {
    case drift::MaskShape::Rectangle: {
        QPainterPath path;
        path.addRect(QRectF(center.x() - halfW, center.y() - halfH, halfW * 2.0, halfH * 2.0));
        return path;
    }
    case drift::MaskShape::Ellipse: {
        QPainterPath path;
        path.addEllipse(center, halfW, halfH);
        return path;
    }
    case drift::MaskShape::Star:
        return drift::regularPolygonPath(squareBounds(center, qMin(halfW, halfH)), 5, mask.rotation);
    case drift::MaskShape::Heart:
        return drift::heartPath(squareBounds(center, qMin(halfW, halfH)));
    case drift::MaskShape::Bars: {
        const double barH = halfH;
        QPainterPath path;
        path.addRect(QRectF(0, 0, canvasWidth, barH));
        path.addRect(QRectF(0, canvasHeight - barH, canvasWidth, barH));
        return path;
    }
    case drift::MaskShape::Freeform: {
        QPainterPath path;
        if (mask.points.isEmpty())
            return path;
        for (int i = 0; i < mask.points.size(); ++i) {
            const QPointF pt(mask.points.at(i).x() * canvasWidth, mask.points.at(i).y() * canvasHeight);
            if (i == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        path.closeSubpath();
        return path;
    }
    case drift::MaskShape::Matte:
        // Raster, not parametric: the coverage map is decoded per frame in FrameCompositor and
        // rides on GpuLayer::matte. There is no path to rasterize.
        break;
    case drift::MaskShape::None:
        break;
    }
    return {};
}

QImage blurAlpha(const QImage &alpha, int radius)
{
    if (radius <= 0 || alpha.isNull())
        return alpha;

    QImage out = alpha;
    const int passes = qBound(1, radius / 2, 8);
    for (int pass = 0; pass < passes; ++pass) {
        QImage blurred(out.size(), QImage::Format_Grayscale8);
        for (int y = 0; y < out.height(); ++y) {
            auto *line = blurred.scanLine(y);
            for (int x = 0; x < out.width(); ++x) {
                int sum = 0;
                int count = 0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    const int sy = qBound(0, y + dy, out.height() - 1);
                    for (int dx = -radius; dx <= radius; ++dx) {
                        const int sx = qBound(0, x + dx, out.width() - 1);
                        sum += qGray(out.pixel(sx, sy));
                        ++count;
                    }
                }
                line[x] = static_cast<uchar>(sum / qMax(1, count));
            }
        }
        out = blurred;
    }
    return out;
}

} // namespace

namespace drift {

QImage maskAlphaMap(const Mask &mask, int canvasWidth, int canvasHeight)
{
    if (mask.shape == MaskShape::None || mask.shape == MaskShape::Matte || canvasWidth <= 0
        || canvasHeight <= 0)
        return {};

    QImage alpha(canvasWidth, canvasHeight, QImage::Format_Grayscale8);
    alpha.fill(mask.invert ? 255 : 0);

    QPainter mp(&alpha);
    mp.setRenderHint(QPainter::Antialiasing);
    mp.setBrush(mask.invert ? Qt::black : Qt::white);
    mp.setPen(Qt::NoPen);

    QPainterPath path = maskPath(mask, canvasWidth, canvasHeight);
    if (!path.isEmpty()) {
        const QPointF center(mask.x * canvasWidth, mask.y * canvasHeight);
        if (!qFuzzyIsNull(mask.rotation) && mask.shape != MaskShape::Bars && mask.shape != MaskShape::Freeform) {
            QTransform transform;
            transform.translate(center.x(), center.y());
            transform.rotate(mask.rotation);
            transform.translate(-center.x(), -center.y());
            path = transform.map(path);
        }
        mp.drawPath(path);
    }
    mp.end();

    if (mask.feather > 0.0)
        alpha = blurAlpha(alpha, qMax(1, static_cast<int>(mask.feather)));

    return alpha;
}

QImage applyMask(const QImage &frame, const Mask &mask, int canvasWidth, int canvasHeight)
{
    if (mask.shape == MaskShape::None || mask.shape == MaskShape::Matte || frame.isNull())
        return frame;

    const QImage alpha = maskAlphaMap(mask, canvasWidth, canvasHeight);
    if (alpha.isNull())
        return frame;

  QImage rgba = frame.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.size() != alpha.size())
        rgba = rgba.scaled(canvasWidth, canvasHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QImage out(rgba.size(), QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(rgba.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        const uchar *alphaLine = alpha.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            const int a = qAlpha(src[x]) * alphaLine[x] / 255;
            dst[x] = qRgba(qRed(src[x]), qGreen(src[x]), qBlue(src[x]), a);
        }
    }
    return out;
}

} // namespace drift

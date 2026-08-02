#include "FadeShape.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace drift {

QString fadeCurveToString(FadeCurve curve)
{
    switch (curve) {
    case FadeCurve::Linear:
        return QStringLiteral("linear");
    case FadeCurve::Smooth:
        return QStringLiteral("smooth");
    case FadeCurve::EqualPower:
        return QStringLiteral("equalPower");
    case FadeCurve::Custom:
        return QStringLiteral("custom");
    }
    return QStringLiteral("smooth");
}

FadeCurve fadeCurveFromString(const QString &curve)
{
    if (curve == QStringLiteral("linear"))
        return FadeCurve::Linear;
    if (curve == QStringLiteral("equalPower"))
        return FadeCurve::EqualPower;
    if (curve == QStringLiteral("custom"))
        return FadeCurve::Custom;
    return FadeCurve::Smooth;
}

void FadeShape::setPoints(QList<QPointF> points)
{
    if (points.size() < 2) {
        m_points = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
        return;
    }

    std::sort(points.begin(), points.end(),
              [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });

    QList<QPointF> cleaned;
    cleaned.reserve(points.size());
    for (QPointF p : points) {
        p.setX(qBound(0.0, p.x(), 1.0));
        p.setY(qBound(0.0, p.y(), 1.0));
        if (!cleaned.isEmpty() && qAbs(cleaned.last().x() - p.x()) < 1e-9)
            cleaned.last().setY(p.y());
        else
            cleaned.append(p);
    }

    if (cleaned.isEmpty() || cleaned.first().x() > 1e-9)
        cleaned.prepend(QPointF(0.0, 0.0));
    else
        cleaned.first() = QPointF(0.0, cleaned.first().y());

    if (cleaned.last().x() < 1.0 - 1e-9)
        cleaned.append(QPointF(1.0, 1.0));
    else
        cleaned.last() = QPointF(1.0, cleaned.last().y());

    // Ends stay silent → full: pin gain at the rails.
    cleaned.first().setY(0.0);
    cleaned.last().setY(1.0);

    if (cleaned.size() < 2)
        cleaned = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};

    m_points = cleaned;
}

void FadeShape::clear()
{
    m_points.clear();
}

double FadeShape::gainAt(double t) const
{
    t = qBound(0.0, t, 1.0);
    if (isEmpty())
        return t; // linear fallback

    if (t <= m_points.first().x())
        return m_points.first().y();
    if (t >= m_points.last().x())
        return m_points.last().y();

    for (int i = 0; i + 1 < m_points.size(); ++i) {
        const QPointF &a = m_points.at(i);
        const QPointF &b = m_points.at(i + 1);
        if (t < a.x() || t > b.x())
            continue;
        const double span = b.x() - a.x();
        if (span < 1e-12)
            return b.y();
        const double u = (t - a.x()) / span;
        return a.y() + (b.y() - a.y()) * u;
    }
    return m_points.last().y();
}

QJsonArray FadeShape::toJson() const
{
    QJsonArray array;
    for (const QPointF &p : m_points) {
        array.append(QJsonObject{
            {QStringLiteral("t"), p.x()},
            {QStringLiteral("g"), p.y()},
        });
    }
    return array;
}

FadeShape FadeShape::fromJson(const QJsonArray &array)
{
    QList<QPointF> points;
    points.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        points.append(QPointF(obj.value(QStringLiteral("t")).toDouble(),
                              obj.value(QStringLiteral("g")).toDouble()));
    }
    FadeShape shape;
    shape.setPoints(points);
    return shape;
}

FadeShape FadeShape::linearPreset()
{
    FadeShape shape;
    // Keep a middle handle so the custom editor always has something to drag.
    shape.setPoints({QPointF(0.0, 0.0), QPointF(0.5, 0.5), QPointF(1.0, 1.0)});
    return shape;
}

FadeShape FadeShape::smoothPreset()
{
    QList<QPointF> points;
    constexpr int kSamples = 9;
    for (int i = 0; i < kSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSamples - 1);
        const double g = t * t * (3.0 - 2.0 * t);
        points.append(QPointF(t, g));
    }
    FadeShape shape;
    shape.setPoints(points);
    return shape;
}

FadeShape FadeShape::equalPowerPreset()
{
    QList<QPointF> points;
    constexpr int kSamples = 9;
    constexpr double kHalfPi = 1.5707963267948966;
    for (int i = 0; i < kSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSamples - 1);
        points.append(QPointF(t, std::sin(t * kHalfPi)));
    }
    FadeShape shape;
    shape.setPoints(points);
    return shape;
}

} // namespace drift

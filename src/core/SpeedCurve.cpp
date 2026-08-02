#include "SpeedCurve.h"

#include "Bezier.h"

#include <QJsonObject>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

// Buckets in the flattened table. The curve is smooth, so this is far finer than the frame
// grid it ends up quantised to.
constexpr int kSamples = 256;
constexpr double kFlatEpsilon = 1e-9;

// The cubic helpers live in Bezier.h — keyframes grew tangent handles of their own, and both
// curve kinds now share one implementation.

} // namespace

void SpeedCurve::setPoints(QList<SpeedPoint> points)
{
    if (points.size() < 2) {
        clear();
        return;
    }

    std::sort(points.begin(), points.end(),
              [](const SpeedPoint &a, const SpeedPoint &b) { return a.pos < b.pos; });

    for (SpeedPoint &point : points) {
        point.pos = qBound(0.0, point.pos, 1.0);
        point.speed = qBound(kMinCurveSpeed, point.speed, kMaxCurveSpeed);
    }
    points.first().pos = 0.0;
    points.last().pos = 1.0;

    // A handle reaching past its neighbour would fold the curve back on itself and make the
    // speed multi-valued, which the integration below cannot represent.
    for (int i = 0; i < points.size(); ++i) {
        SpeedPoint &point = points[i];
        const double toPrev = i > 0 ? point.pos - points.at(i - 1).pos : 0.0;
        const double toNext = i + 1 < points.size() ? points.at(i + 1).pos - point.pos : 0.0;
        point.inDx = qBound(-toPrev, point.inDx, 0.0);
        point.outDx = qBound(0.0, point.outDx, toNext);
    }

    m_points = points;
    rebuild();
}

void SpeedCurve::clear()
{
    m_points.clear();
    m_speeds.clear();
    m_cum.clear();
}

void SpeedCurve::rebuild()
{
    m_speeds.resize(kSamples + 1);
    m_cum.resize(kSamples + 1);

    int segment = 0;
    for (int i = 0; i <= kSamples; ++i) {
        const double pos = static_cast<double>(i) / kSamples;
        while (segment + 2 < m_points.size() && m_points.at(segment + 1).pos < pos)
            ++segment;

        const SpeedPoint &a = m_points.at(segment);
        const SpeedPoint &b = m_points.at(segment + 1);
        double speed;
        if (b.pos - a.pos <= kFlatEpsilon) {
            speed = b.speed;
        } else {
            const double t = bezierParameterForX(a.pos, a.pos + a.outDx, b.pos + b.inDx, b.pos, pos);
            speed = cubicBezier(a.speed, a.speed + a.outDy, b.speed + b.inDy, b.speed, t);
        }
        // Handles can overshoot in y even when they behave in x.
        m_speeds[i] = qBound(kMinCurveSpeed, speed, kMaxCurveSpeed);
    }

    // Time to cross each bucket, with speed ramping linearly across it:
    //   ∫dp/v = (dp / Δv)·ln(v₁/v₀), degenerating to dp/v when the bucket is flat.
    const double dp = 1.0 / kSamples;
    m_cum[0] = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        const double v0 = m_speeds.at(i);
        const double v1 = m_speeds.at(i + 1);
        const double dv = v1 - v0;
        const double dt = std::abs(dv) < kFlatEpsilon ? dp / v0 : dp * std::log(v1 / v0) / dv;
        m_cum[i + 1] = m_cum.at(i) + dt;
    }
}

double SpeedCurve::speedAt(double pos) const
{
    if (isEmpty())
        return 1.0;

    const double x = qBound(0.0, pos, 1.0) * kSamples;
    const int i = qMin(static_cast<int>(x), kSamples - 1);
    const double f = x - i;
    return m_speeds.at(i) + (m_speeds.at(i + 1) - m_speeds.at(i)) * f;
}

TimeUs SpeedCurve::retimedDurationUs(TimeUs sourceSpanUs) const
{
    if (isEmpty() || sourceSpanUs <= 0)
        return sourceSpanUs;
    return static_cast<TimeUs>(std::llround(m_cum.last() * static_cast<double>(sourceSpanUs)));
}

int SpeedCurve::bucketForUnitTime(double unitTime, double *localOut) const
{
    const double clamped = qBound(0.0, unitTime, m_cum.last());
    const auto it = std::upper_bound(m_cum.constBegin(), m_cum.constEnd(), clamped);
    const int i = qBound(0, static_cast<int>(it - m_cum.constBegin()) - 1, kSamples - 1);
    *localOut = clamped - m_cum.at(i);
    return i;
}

TimeUs SpeedCurve::sourceOffsetForTimelineOffset(TimeUs timelineOffsetUs, TimeUs sourceSpanUs) const
{
    if (isEmpty() || sourceSpanUs <= 0)
        return timelineOffsetUs;

    double local = 0.0;
    const int i = bucketForUnitTime(static_cast<double>(timelineOffsetUs) / sourceSpanUs, &local);

    const double dp = 1.0 / kSamples;
    const double v0 = m_speeds.at(i);
    const double v1 = m_speeds.at(i + 1);
    const double dv = v1 - v0;

    // Inverse of the bucket integral above: v grows exponentially in elapsed time.
    double pos = static_cast<double>(i) * dp;
    if (std::abs(dv) < kFlatEpsilon) {
        pos += local * v0;
    } else {
        const double k = dv / dp;
        pos += (v0 * std::exp(local * k) - v0) / k;
    }

    return static_cast<TimeUs>(std::llround(qBound(0.0, pos, 1.0) * static_cast<double>(sourceSpanUs)));
}

TimeUs SpeedCurve::timelineOffsetForSourceOffset(TimeUs sourceOffsetUs, TimeUs sourceSpanUs) const
{
    if (isEmpty() || sourceSpanUs <= 0)
        return sourceOffsetUs;

    const double x = qBound(0.0, static_cast<double>(sourceOffsetUs) / sourceSpanUs, 1.0) * kSamples;
    const int i = qMin(static_cast<int>(x), kSamples - 1);
    const double frac = x - i;

    // Whole buckets are already summed; only the partial one at the end needs integrating.
    const double dp = frac / kSamples;
    const double v0 = m_speeds.at(i);
    const double v = v0 + (m_speeds.at(i + 1) - v0) * frac;
    const double dv = v - v0;
    const double partial = std::abs(dv) < kFlatEpsilon ? dp / v0 : dp * std::log(v / v0) / dv;

    return static_cast<TimeUs>(std::llround((m_cum.at(i) + partial) * static_cast<double>(sourceSpanUs)));
}

double SpeedCurve::speedAtTimelineOffset(TimeUs timelineOffsetUs, TimeUs sourceSpanUs) const
{
    if (isEmpty() || sourceSpanUs <= 0)
        return 1.0;

    double local = 0.0;
    const int i = bucketForUnitTime(static_cast<double>(timelineOffsetUs) / sourceSpanUs, &local);

    const double dp = 1.0 / kSamples;
    const double v0 = m_speeds.at(i);
    const double dv = m_speeds.at(i + 1) - v0;
    if (std::abs(dv) < kFlatEpsilon)
        return v0;
    return qBound(kMinCurveSpeed, v0 * std::exp(local * dv / dp), kMaxCurveSpeed);
}

SpeedCurve SpeedCurve::subRange(double p0, double p1) const
{
    SpeedCurve out;
    if (isEmpty() || p1 - p0 <= kFlatEpsilon)
        return out;

    // Resampled as plain corner points: the shape is what has to survive a split, not the
    // handles that happened to produce it.
    constexpr int kResampled = 33;
    QList<SpeedPoint> points;
    points.reserve(kResampled);
    for (int i = 0; i < kResampled; ++i) {
        const double f = static_cast<double>(i) / (kResampled - 1);
        SpeedPoint point;
        point.pos = f;
        point.speed = speedAt(p0 + (p1 - p0) * f);
        point.corner = true;
        points.append(point);
    }
    out.setPoints(points);
    return out;
}

QJsonArray SpeedCurve::toJson() const
{
    QJsonArray array;
    for (const SpeedPoint &point : m_points) {
        array.append(QJsonObject{
            {QStringLiteral("pos"), point.pos},
            {QStringLiteral("speed"), point.speed},
            {QStringLiteral("inDx"), point.inDx},
            {QStringLiteral("inDy"), point.inDy},
            {QStringLiteral("outDx"), point.outDx},
            {QStringLiteral("outDy"), point.outDy},
            {QStringLiteral("corner"), point.corner},
        });
    }
    return array;
}

SpeedCurve SpeedCurve::fromJson(const QJsonArray &array)
{
    QList<SpeedPoint> points;
    points.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        SpeedPoint point;
        point.pos = object.value(QStringLiteral("pos")).toDouble();
        point.speed = object.value(QStringLiteral("speed")).toDouble(1.0);
        point.inDx = object.value(QStringLiteral("inDx")).toDouble();
        point.inDy = object.value(QStringLiteral("inDy")).toDouble();
        point.outDx = object.value(QStringLiteral("outDx")).toDouble();
        point.outDy = object.value(QStringLiteral("outDy")).toDouble();
        point.corner = object.value(QStringLiteral("corner")).toBool();
        points.append(point);
    }

    SpeedCurve curve;
    curve.setPoints(points);
    return curve;
}

SpeedCurve SpeedCurve::flat(double speed)
{
    SpeedPoint start;
    start.pos = 0.0;
    start.speed = speed;
    SpeedPoint end;
    end.pos = 1.0;
    end.speed = speed;

    SpeedCurve curve;
    curve.setPoints({start, end});
    return curve;
}

} // namespace drift

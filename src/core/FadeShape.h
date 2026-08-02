#pragma once

#include <QJsonArray>
#include <QList>
#include <QPointF>
#include <QString>

#include <cmath>

namespace drift {

// Style for edge fades and CapCut-style intro/outro progress (Linear / Smooth /
// Natural / Custom). Natural is equal-power — nicer for audio and soft fades.
enum class FadeCurve { Linear, Smooth, EqualPower, Custom };

QString fadeCurveToString(FadeCurve curve);
FadeCurve fadeCurveFromString(const QString &curve);

// Unit gain curve for fades and animation progress: t in [0,1] → gain in [0,1].
// Piecewise-linear between sorted control points; ends are pinned to (0,0) and (1,1).
class FadeShape
{
public:
    bool isEmpty() const { return m_points.size() < 2; }

    const QList<QPointF> &points() const { return m_points; }
    void setPoints(QList<QPointF> points);
    void clear();

    double gainAt(double t) const;

    QJsonArray toJson() const;
    static FadeShape fromJson(const QJsonArray &array);

    // Seed matching Smooth (smoothstep).
    static FadeShape smoothPreset();
    // Straight diagonal — Linear preset.
    static FadeShape linearPreset();
    // Equal-power (Natural) seed.
    static FadeShape equalPowerPreset();

private:
    QList<QPointF> m_points;
};

inline double shapedProgress(double t, FadeCurve curve, const FadeShape &shape)
{
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    switch (curve) {
    case FadeCurve::Linear:
        return t;
    case FadeCurve::Smooth:
        return t * t * (3.0 - 2.0 * t);
    case FadeCurve::EqualPower:
        return std::sin(t * 1.5707963267948966); // t * pi/2
    case FadeCurve::Custom:
        return shape.isEmpty() ? t : shape.gainAt(t);
    }
    return t;
}

} // namespace drift

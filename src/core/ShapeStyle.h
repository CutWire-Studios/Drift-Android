#pragma once

#include <QColor>
#include <QList>
#include <QString>

namespace drift {

enum class ShapeKind {
    // Basic
    Rectangle,
    RoundedRectangle,
    Square,
    Ellipse,
    Triangle,
    RightTriangle,
    Diamond,
    Pentagon,
    Hexagon,
    Octagon,
    Parallelogram,
    Trapezoid,
    // Arrows
    Arrow,
    DoubleArrow,
    BlockArrow,
    CurvedArrow,
    Chevron,
    // Bubbles
    SpeechBubble,
    SpeechBubbleRect,
    ThoughtBubble,
    Callout,
    // Fun
    Star,
    LightningBolt,
    Cloud,
    Heart,
    Cross,
    Burst,
    Banner,
};

enum class ShapeFillKind { None, Solid, LinearGradient, RadialGradient };
enum class ShapeStrokeStyle { None, Solid, Dash, Dot, DashDot };

QString shapeKindToString(ShapeKind kind);
ShapeKind shapeKindFromString(const QString &kind);

QString shapeFillKindToString(ShapeFillKind kind);
ShapeFillKind shapeFillKindFromString(const QString &kind);

QString shapeStrokeStyleToString(ShapeStrokeStyle style);
ShapeStrokeStyle shapeStrokeStyleFromString(const QString &style);

struct ShapeStyle
{
    ShapeKind kind = ShapeKind::Rectangle;

    ShapeFillKind fillKind = ShapeFillKind::Solid;
    QColor fill = QColor(0, 180, 255);
    QColor fillSecondary = QColor(122, 0, 255); // gradient end stop
    double gradientAngle = 90.0;                // degrees; 0 = left → right

    QColor stroke = Qt::white;
    double strokeWidth = 4.0;
    ShapeStrokeStyle strokeStyle = ShapeStrokeStyle::Solid;

    // Geometry knobs. Each is read by a subset of kinds only; see ShapePath.cpp.
    double cornerRadius = 0.0; // project px — rect family, rect bubble, callout
    int points = 5;            // star / burst spikes
    double innerRatio = 0.5;   // star / burst inner radius, fraction of outer
    double headSize = 0.4;     // arrows / chevron / banner: head length, fraction of width
    double thickness = 0.4;    // arrows / chevron / cross: shaft, fraction of height
    double tailX = 0.25;       // bubbles: tail anchor along the bottom edge, 0..1
    double tailSize = 0.2;     // bubbles: tail extent, fraction of height
};

// The shapes offered in the assets panel. Several entries can share a ShapeKind and differ only in
// their default aspect or colour ("circle" vs "ellipse"), so ids — not kinds — are what the UI and
// the drag mime data carry.
struct ShapeCatalogEntry
{
    QString id;
    QString label;
    QString category;
    double aspect; // default layout box width / height
    ShapeStyle style;
};

struct ShapeCategory
{
    QString id;
    QString label;
};

const QList<ShapeCatalogEntry> &shapeCatalog();
const ShapeCatalogEntry *shapeCatalogEntry(const QString &id);
QList<ShapeCategory> shapeCategories();

} // namespace drift

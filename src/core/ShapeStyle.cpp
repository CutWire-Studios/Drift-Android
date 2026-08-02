#include "ShapeStyle.h"

#include <algorithm>

namespace drift {
namespace {

struct KindName
{
    ShapeKind kind;
    const char *name;
};

// The first five names are what projects saved before the roster grew still contain, so they must
// not change.
constexpr KindName kKindNames[] = {
    {ShapeKind::Rectangle, "rectangle"},
    {ShapeKind::Square, "square"},
    {ShapeKind::Triangle, "triangle"},
    {ShapeKind::Pentagon, "pentagon"},
    {ShapeKind::Hexagon, "hexagon"},
    {ShapeKind::RoundedRectangle, "rounded-rectangle"},
    {ShapeKind::Ellipse, "ellipse"},
    {ShapeKind::RightTriangle, "right-triangle"},
    {ShapeKind::Diamond, "diamond"},
    {ShapeKind::Octagon, "octagon"},
    {ShapeKind::Parallelogram, "parallelogram"},
    {ShapeKind::Trapezoid, "trapezoid"},
    {ShapeKind::Arrow, "arrow"},
    {ShapeKind::DoubleArrow, "double-arrow"},
    {ShapeKind::BlockArrow, "block-arrow"},
    {ShapeKind::CurvedArrow, "curved-arrow"},
    {ShapeKind::Chevron, "chevron"},
    {ShapeKind::SpeechBubble, "speech-bubble"},
    {ShapeKind::SpeechBubbleRect, "speech-bubble-rect"},
    {ShapeKind::ThoughtBubble, "thought-bubble"},
    {ShapeKind::Callout, "callout"},
    {ShapeKind::Star, "star"},
    {ShapeKind::LightningBolt, "lightning-bolt"},
    {ShapeKind::Cloud, "cloud"},
    {ShapeKind::Heart, "heart"},
    {ShapeKind::Cross, "cross"},
    {ShapeKind::Burst, "burst"},
    {ShapeKind::Banner, "banner"},
};

ShapeStyle makeStyle(ShapeKind kind, const QColor &fill, const QColor &fillSecondary)
{
    ShapeStyle style;
    style.kind = kind;
    style.fill = fill;
    style.fillSecondary = fillSecondary;
    return style;
}

QList<ShapeCatalogEntry> buildCatalog()
{
    const QColor blue(0, 180, 255);
    const QColor blueDeep(0, 92, 220);
    const QColor orange(255, 120, 64);
    const QColor orangeDeep(220, 60, 40);
    const QColor yellow(255, 214, 10);
    const QColor yellowDeep(240, 140, 0);
    const QColor purple(160, 96, 255);
    const QColor purpleDeep(96, 40, 200);
    const QColor green(80, 220, 140);
    const QColor greenDeep(20, 150, 110);
    const QColor pink(255, 96, 150);
    const QColor pinkDeep(200, 30, 110);

    QList<ShapeCatalogEntry> catalog{
        {QStringLiteral("rectangle"), QStringLiteral("Rectangle"), QStringLiteral("basic"), 1.6,
         makeStyle(ShapeKind::Rectangle, blue, blueDeep)},
        {QStringLiteral("rounded-rectangle"), QStringLiteral("Rounded rectangle"),
         QStringLiteral("basic"), 1.6, makeStyle(ShapeKind::RoundedRectangle, blue, blueDeep)},
        {QStringLiteral("square"), QStringLiteral("Square"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Square, orange, orangeDeep)},
        // "ellipse" leads "circle" because the inspector's kind list keeps the first entry of each
        // ShapeKind, and "Ellipse" is the honest label for both.
        {QStringLiteral("ellipse"), QStringLiteral("Ellipse"), QStringLiteral("basic"), 1.6,
         makeStyle(ShapeKind::Ellipse, yellow, yellowDeep)},
        {QStringLiteral("circle"), QStringLiteral("Circle"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Ellipse, yellow, yellowDeep)},
        {QStringLiteral("triangle"), QStringLiteral("Triangle"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Triangle, yellow, yellowDeep)},
        {QStringLiteral("right-triangle"), QStringLiteral("Right triangle"), QStringLiteral("basic"),
         1.0, makeStyle(ShapeKind::RightTriangle, yellow, yellowDeep)},
        {QStringLiteral("diamond"), QStringLiteral("Diamond"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Diamond, blue, blueDeep)},
        {QStringLiteral("pentagon"), QStringLiteral("Pentagon"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Pentagon, purple, purpleDeep)},
        {QStringLiteral("hexagon"), QStringLiteral("Hexagon"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Hexagon, green, greenDeep)},
        {QStringLiteral("octagon"), QStringLiteral("Octagon"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Octagon, green, greenDeep)},
        {QStringLiteral("parallelogram"), QStringLiteral("Parallelogram"), QStringLiteral("basic"),
         1.6, makeStyle(ShapeKind::Parallelogram, blue, blueDeep)},
        {QStringLiteral("trapezoid"), QStringLiteral("Trapezoid"), QStringLiteral("basic"), 1.6,
         makeStyle(ShapeKind::Trapezoid, blue, blueDeep)},

        {QStringLiteral("arrow"), QStringLiteral("Arrow"), QStringLiteral("arrows"), 2.0,
         makeStyle(ShapeKind::Arrow, orange, orangeDeep)},
        {QStringLiteral("double-arrow"), QStringLiteral("Double arrow"), QStringLiteral("arrows"),
         2.0, makeStyle(ShapeKind::DoubleArrow, orange, orangeDeep)},
        {QStringLiteral("block-arrow"), QStringLiteral("Block arrow"), QStringLiteral("arrows"), 1.6,
         makeStyle(ShapeKind::BlockArrow, orange, orangeDeep)},
        {QStringLiteral("curved-arrow"), QStringLiteral("Curved arrow"), QStringLiteral("arrows"),
         1.3, makeStyle(ShapeKind::CurvedArrow, orange, orangeDeep)},
        {QStringLiteral("chevron"), QStringLiteral("Chevron"), QStringLiteral("arrows"), 1.4,
         makeStyle(ShapeKind::Chevron, orange, orangeDeep)},

        {QStringLiteral("speech-bubble"), QStringLiteral("Speech bubble"), QStringLiteral("bubbles"),
         1.4, makeStyle(ShapeKind::SpeechBubble, blue, blueDeep)},
        {QStringLiteral("speech-bubble-rect"), QStringLiteral("Rounded bubble"),
         QStringLiteral("bubbles"), 1.5, makeStyle(ShapeKind::SpeechBubbleRect, blue, blueDeep)},
        {QStringLiteral("thought-bubble"), QStringLiteral("Thought bubble"),
         QStringLiteral("bubbles"), 1.4, makeStyle(ShapeKind::ThoughtBubble, blue, blueDeep)},
        {QStringLiteral("callout"), QStringLiteral("Callout"), QStringLiteral("bubbles"), 1.5,
         makeStyle(ShapeKind::Callout, purple, purpleDeep)},

        {QStringLiteral("star"), QStringLiteral("Star"), QStringLiteral("fun"), 1.0,
         makeStyle(ShapeKind::Star, yellow, yellowDeep)},
        {QStringLiteral("burst"), QStringLiteral("Burst"), QStringLiteral("fun"), 1.0,
         makeStyle(ShapeKind::Burst, yellow, orangeDeep)},
        {QStringLiteral("lightning-bolt"), QStringLiteral("Lightning bolt"), QStringLiteral("fun"),
         0.6, makeStyle(ShapeKind::LightningBolt, yellow, orangeDeep)},
        {QStringLiteral("cloud"), QStringLiteral("Cloud"), QStringLiteral("fun"), 1.6,
         makeStyle(ShapeKind::Cloud, QColor(235, 245, 255), QColor(150, 190, 235))},
        {QStringLiteral("heart"), QStringLiteral("Heart"), QStringLiteral("fun"), 1.0,
         makeStyle(ShapeKind::Heart, pink, pinkDeep)},
        {QStringLiteral("cross"), QStringLiteral("Cross"), QStringLiteral("fun"), 1.0,
         makeStyle(ShapeKind::Cross, pink, pinkDeep)},
        {QStringLiteral("banner"), QStringLiteral("Banner"), QStringLiteral("fun"), 2.2,
         makeStyle(ShapeKind::Banner, purple, purpleDeep)},
    };

    // Per-kind geometry defaults. Everything not listed keeps the ShapeStyle defaults.
    for (ShapeCatalogEntry &entry : catalog) {
        switch (entry.style.kind) {
        case ShapeKind::RoundedRectangle:
        case ShapeKind::SpeechBubbleRect:
        case ShapeKind::Callout:
            entry.style.cornerRadius = 32.0;
            break;
        case ShapeKind::Burst:
            entry.style.points = 12;
            entry.style.innerRatio = 0.62;
            break;
        case ShapeKind::Cross:
            entry.style.thickness = 0.36;
            break;
        case ShapeKind::Chevron:
            entry.style.headSize = 0.3;
            break;
        case ShapeKind::CurvedArrow:
            entry.style.thickness = 0.35;
            break;
        default:
            break;
        }
    }

    return catalog;
}

} // namespace

QString shapeKindToString(ShapeKind kind)
{
    for (const KindName &entry : kKindNames) {
        if (entry.kind == kind)
            return QString::fromLatin1(entry.name);
    }
    return QStringLiteral("rectangle");
}

ShapeKind shapeKindFromString(const QString &kind)
{
    for (const KindName &entry : kKindNames) {
        if (kind == QLatin1String(entry.name))
            return entry.kind;
    }
    // Catalog ids are not all kind names ("circle" is an ellipse), and the UI addresses shapes by
    // id, so fall back to a catalog lookup before giving up.
    if (const ShapeCatalogEntry *entry = shapeCatalogEntry(kind))
        return entry->style.kind;
    return ShapeKind::Rectangle;
}

QString shapeFillKindToString(ShapeFillKind kind)
{
    switch (kind) {
    case ShapeFillKind::None:
        return QStringLiteral("none");
    case ShapeFillKind::Solid:
        return QStringLiteral("solid");
    case ShapeFillKind::LinearGradient:
        return QStringLiteral("linear");
    case ShapeFillKind::RadialGradient:
        return QStringLiteral("radial");
    }
    return QStringLiteral("solid");
}

ShapeFillKind shapeFillKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("none"))
        return ShapeFillKind::None;
    if (kind == QStringLiteral("linear"))
        return ShapeFillKind::LinearGradient;
    if (kind == QStringLiteral("radial"))
        return ShapeFillKind::RadialGradient;
    return ShapeFillKind::Solid;
}

QString shapeStrokeStyleToString(ShapeStrokeStyle style)
{
    switch (style) {
    case ShapeStrokeStyle::None:
        return QStringLiteral("none");
    case ShapeStrokeStyle::Solid:
        return QStringLiteral("solid");
    case ShapeStrokeStyle::Dash:
        return QStringLiteral("dash");
    case ShapeStrokeStyle::Dot:
        return QStringLiteral("dot");
    case ShapeStrokeStyle::DashDot:
        return QStringLiteral("dashdot");
    }
    return QStringLiteral("solid");
}

ShapeStrokeStyle shapeStrokeStyleFromString(const QString &style)
{
    if (style == QStringLiteral("none"))
        return ShapeStrokeStyle::None;
    if (style == QStringLiteral("dash"))
        return ShapeStrokeStyle::Dash;
    if (style == QStringLiteral("dot"))
        return ShapeStrokeStyle::Dot;
    if (style == QStringLiteral("dashdot"))
        return ShapeStrokeStyle::DashDot;
    return ShapeStrokeStyle::Solid;
}

const QList<ShapeCatalogEntry> &shapeCatalog()
{
    static const QList<ShapeCatalogEntry> catalog = buildCatalog();
    return catalog;
}

const ShapeCatalogEntry *shapeCatalogEntry(const QString &id)
{
    const QList<ShapeCatalogEntry> &catalog = shapeCatalog();
    const auto it = std::find_if(catalog.cbegin(), catalog.cend(),
                                 [&](const ShapeCatalogEntry &e) { return e.id == id; });
    return it == catalog.cend() ? nullptr : &(*it);
}

QList<ShapeCategory> shapeCategories()
{
    return {
        {QStringLiteral("basic"), QStringLiteral("Basic")},
        {QStringLiteral("arrows"), QStringLiteral("Arrows")},
        {QStringLiteral("bubbles"), QStringLiteral("Bubbles")},
        {QStringLiteral("fun"), QStringLiteral("Fun")},
    };
}

} // namespace drift

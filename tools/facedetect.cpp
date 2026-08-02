// Headless face-landmark check: runs FaceLandmarker over one frame and writes an annotated PNG
// plus the anchors as JSON. The overlay is the only practical way to confirm the ROI affine and
// the MediaPipe landmark indices are right, so keep it working.
//
// With --effect it also bakes a one-frame track and pushes the frame through the real effect
// chain, which is how the face shaders get checked without launching the app.
//
//   facedetect <image-or-video> [--time <seconds>] [--out overlay.png]
//              [--effect <id>] [--param k=v] [--effect-out warped.png]

#include "engine/ClipReaderPool.h"
#include "engine/EffectCatalog.h"
#include "engine/EffectProcessor.h"
#include "engine/FaceLandmarker.h"
#include "engine/FaceTrack.h"
#include "core/Effect.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTextStream>

namespace {

void mark(QPainter &p, const QPointF &uv, const QSize &size, const QColor &color,
          const QString &label)
{
    const QPointF px(uv.x() * size.width(), uv.y() * size.height());
    p.setPen(QPen(color, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(px, 4.0, 4.0);
    p.drawText(px + QPointF(7, -7), label);
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        err << "usage: facedetect <image-or-video> [--time <seconds>] [--out overlay.png]\n";
        return 2;
    }

    const QString input = args.at(1);
    double seconds = 0.0;
    QString overlayPath = QStringLiteral("facedetect-overlay.png");
    QString effectId;
    QString effectOut = QStringLiteral("facedetect-effect.png");
    QMap<QString, QVariant> overrides;
    for (int i = 2; i + 1 < args.size(); i += 2) {
        const QString key = args.at(i);
        const QString value = args.at(i + 1);
        if (key == QLatin1String("--time"))
            seconds = value.toDouble();
        else if (key == QLatin1String("--out"))
            overlayPath = value;
        else if (key == QLatin1String("--effect"))
            effectId = value;
        else if (key == QLatin1String("--effect-out"))
            effectOut = value;
        else if (key == QLatin1String("--param") && value.contains(QLatin1Char('=')))
            overrides.insert(value.section(QLatin1Char('='), 0, 0),
                             value.section(QLatin1Char('='), 1));
    }

    QImage frame(input);
    if (frame.isNull()) {
        frame = ClipReaderPool::instance().readVideoFrame(
            input, drift::TimeUs(seconds * drift::kUsPerSecond), 1920, 1080);
    }
    if (frame.isNull()) {
        err << "could not read a frame from " << input << "\n";
        return 1;
    }

    drift::FaceLandmarker &fl = drift::FaceLandmarker::instance();
    if (!fl.available()) {
        err << fl.lastError() << "\n";
        return 1;
    }

    const QList<drift::FaceAnchors> faces = fl.detect(frame);
    out << "frame " << frame.width() << "x" << frame.height() << ", faces: " << faces.size()
        << "\n";

    QImage overlay = frame.convertToFormat(QImage::Format_RGBA8888);
    QPainter p(&overlay);
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont font = p.font();
    font.setPixelSize(qMax(10, overlay.height() / 60));
    p.setFont(font);

    QJsonArray json;
    for (int i = 0; i < faces.size(); ++i) {
        const drift::FaceAnchors &a = faces.at(i);
        out << "  face " << i << " valid=" << a.valid << " score=" << a.score
            << " angle=" << (a.angle * 180.0 / M_PI) << "deg rx=" << a.faceRx << " ry=" << a.faceRy
            << " eyeR=" << a.eyeRadius << "\n";
        if (!a.valid)
            continue;

        mark(p, a.leftEye, overlay.size(), Qt::cyan, QStringLiteral("Leye"));
        mark(p, a.rightEye, overlay.size(), Qt::cyan, QStringLiteral("Reye"));
        mark(p, a.noseTip, overlay.size(), Qt::yellow, QStringLiteral("nose"));
        mark(p, a.mouthCenter, overlay.size(), Qt::magenta, QStringLiteral("mouth"));
        mark(p, a.mouthLeft, overlay.size(), Qt::magenta, QStringLiteral("mL"));
        mark(p, a.mouthRight, overlay.size(), Qt::magenta, QStringLiteral("mR"));
        mark(p, a.chin, overlay.size(), Qt::green, QStringLiteral("chin"));
        mark(p, a.forehead, overlay.size(), Qt::green, QStringLiteral("brow"));
        mark(p, a.faceCenter, overlay.size(), Qt::red, QStringLiteral("c"));

        // The oval, drawn in the same width-normalized space the shaders will use.
        p.save();
        p.translate(a.faceCenter.x() * overlay.width(), a.faceCenter.y() * overlay.height());
        p.rotate(a.angle * 180.0 / M_PI);
        p.setPen(QPen(Qt::red, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(0, 0), a.faceRx * overlay.width(), a.faceRy * overlay.width());
        p.restore();

        QJsonObject o;
        o[QStringLiteral("score")] = a.score;
        o[QStringLiteral("angleDeg")] = a.angle * 180.0 / M_PI;
        o[QStringLiteral("faceRx")] = a.faceRx;
        o[QStringLiteral("faceRy")] = a.faceRy;
        o[QStringLiteral("eyeRadius")] = a.eyeRadius;
        json.append(o);
    }
    p.end();

    if (!overlay.save(overlayPath)) {
        err << "could not write " << overlayPath << "\n";
        return 1;
    }
    out << "wrote " << overlayPath << "\n"
        << QJsonDocument(json).toJson(QJsonDocument::Compact) << "\n";

    if (effectId.isEmpty())
        return 0;

    reloadEffectCatalog();
    const EffectPresetEntry *def = effectDefForId(effectId);
    if (!def) {
        err << "unknown effect '" << effectId << "'\n";
        return 1;
    }
    if (!def->needsFace)
        err << "note: " << effectId << " does not declare requires:face\n";

    // Bake the same anchors into two frames and sample between them, so this also exercises
    // FaceTrack::sample's interpolation rather than only the exact-frame path.
    drift::FaceTrack track;
    track.fps = 30;
    drift::FaceTrackFrame baked;
    baked.faces = faces;
    track.frames = {baked, baked};

    drift::Effect effect;
    effect.catalogId = effectId;
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        effect.parameters.insert(it.key(), it.value());

    const QImage warped = EffectProcessor::applyEffects(
        frame.convertToFormat(QImage::Format_RGBA8888), {effect}, 0,
        track.sampleAll(drift::kUsPerSecond / 60));
    if (warped.isNull() || !warped.save(effectOut)) {
        err << "could not render " << effectId << "\n";
        return 1;
    }
    out << "wrote " << effectOut << " (" << effectId << ")\n";
    return 0;
}

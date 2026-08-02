#include "engine/FaceTrack.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QStandardPaths>

#include <cmath>

namespace drift {
namespace {

constexpr int kFormatVersion = 1;
constexpr int kFieldsPerFace = 24;

// Anchors round-trip as a flat array in this order. Rounded to five decimals: that is a twentieth
// of a pixel across a 4K frame, far below anything a warp can show, and it keeps the sidecar
// roughly a third of the size full doubles would need.
void appendFace(QJsonArray *out, const FaceAnchors &a)
{
    auto round5 = [](double v) { return std::round(v * 100000.0) / 100000.0; };
    QJsonArray f;
    f.append(a.valid ? 1 : 0);
    for (const QPointF &p : {a.leftEye, a.rightEye, a.noseTip, a.mouthCenter, a.mouthLeft,
                             a.mouthRight, a.chin, a.forehead, a.faceCenter}) {
        f.append(round5(p.x()));
        f.append(round5(p.y()));
    }
    f.append(round5(a.faceRx));
    f.append(round5(a.faceRy));
    f.append(round5(a.angle));
    f.append(round5(a.eyeRadius));
    f.append(round5(a.score));
    out->append(f);
}

bool readFace(const QJsonArray &f, FaceAnchors *a)
{
    if (f.size() != kFieldsPerFace)
        return false;
    int i = 0;
    a->valid = f.at(i++).toInt() != 0;
    QPointF *points[] = {&a->leftEye, &a->rightEye,  &a->noseTip, &a->mouthCenter, &a->mouthLeft,
                         &a->mouthRight, &a->chin, &a->forehead, &a->faceCenter};
    for (QPointF *p : points) {
        const double x = f.at(i++).toDouble();
        const double y = f.at(i++).toDouble();
        *p = QPointF(x, y);
    }
    a->faceRx = f.at(i++).toDouble();
    a->faceRy = f.at(i++).toDouble();
    a->angle = f.at(i++).toDouble();
    a->eyeRadius = f.at(i++).toDouble();
    a->score = f.at(i++).toDouble();
    return true;
}

QPointF lerp(const QPointF &a, const QPointF &b, double t)
{
    return a + (b - a) * t;
}

// Angles live on a circle: a face crossing the +/-pi seam would otherwise spin most of the way
// round between two adjacent frames.
double lerpAngle(double a, double b, double t)
{
    double delta = std::fmod(b - a, 2.0 * M_PI);
    if (delta > M_PI)
        delta -= 2.0 * M_PI;
    else if (delta < -M_PI)
        delta += 2.0 * M_PI;
    return a + delta * t;
}

struct CacheEntry
{
    QDateTime modified;
    qint64 size = 0;
    std::shared_ptr<const FaceTrack> track;
};

QMutex g_cacheMutex;
QHash<QString, CacheEntry> g_cache;

} // namespace

FaceAnchors FaceTrack::sample(TimeUs relativeUs, int faceIndex) const
{
    if (frames.isEmpty() || fps <= 0 || faceIndex < 0)
        return {};

    const double frameUs = double(kUsPerSecond) / fps;
    const double exact = double(relativeUs) / frameUs;
    if (exact <= 0.0) {
        const FaceTrackFrame &f = frames.first();
        return faceIndex < f.faces.size() ? f.faces.at(faceIndex) : FaceAnchors{};
    }
    if (exact >= frames.size() - 1) {
        const FaceTrackFrame &f = frames.last();
        return faceIndex < f.faces.size() ? f.faces.at(faceIndex) : FaceAnchors{};
    }

    const int i0 = int(exact);
    const int i1 = i0 + 1;
    const double t = exact - i0;

    const FaceTrackFrame &f0 = frames.at(i0);
    const FaceTrackFrame &f1 = frames.at(i1);
    if (faceIndex >= f0.faces.size() || faceIndex >= f1.faces.size())
        return {};

    const FaceAnchors &a = f0.faces.at(faceIndex);
    const FaceAnchors &b = f1.faces.at(faceIndex);
    if (!a.valid || !b.valid)
        return {};

    FaceAnchors out;
    out.valid = true;
    out.leftEye = lerp(a.leftEye, b.leftEye, t);
    out.rightEye = lerp(a.rightEye, b.rightEye, t);
    out.noseTip = lerp(a.noseTip, b.noseTip, t);
    out.mouthCenter = lerp(a.mouthCenter, b.mouthCenter, t);
    out.mouthLeft = lerp(a.mouthLeft, b.mouthLeft, t);
    out.mouthRight = lerp(a.mouthRight, b.mouthRight, t);
    out.chin = lerp(a.chin, b.chin, t);
    out.forehead = lerp(a.forehead, b.forehead, t);
    out.faceCenter = lerp(a.faceCenter, b.faceCenter, t);
    out.faceRx = a.faceRx + (b.faceRx - a.faceRx) * t;
    out.faceRy = a.faceRy + (b.faceRy - a.faceRy) * t;
    out.angle = lerpAngle(a.angle, b.angle, t);
    out.eyeRadius = a.eyeRadius + (b.eyeRadius - a.eyeRadius) * t;
    out.score = a.score + (b.score - a.score) * t;
    return out;
}

QList<FaceAnchors> FaceTrack::sampleAll(TimeUs relativeUs) const
{
    int slotCount = 0;
    for (const FaceTrackFrame &f : frames)
        slotCount = qMax(slotCount, int(f.faces.size()));

    QList<FaceAnchors> out;
    out.reserve(slotCount);
    for (int i = 0; i < slotCount; ++i)
        out.append(sample(relativeUs, i));
    return out;
}

void applyFaceUniforms(QMap<QString, QVariant> *parameters, const QList<FaceAnchors> &faceSlots)
{
    // Consumed rather than forwarded: "faceIndex" selects the slot and is not a shader uniform.
    const int faceIndex = parameters->take(QStringLiteral("faceIndex")).toInt();
    FaceAnchors face;
    if (faceIndex >= 0 && faceIndex < faceSlots.size())
        face = faceSlots.at(faceIndex);

    parameters->insert(QStringLiteral("u_faceValid"), face.valid ? 1.0 : 0.0);
    if (!face.valid)
        return;

    auto point = [&](const char *name, const QPointF &p) {
        parameters->insert(QLatin1String(name) + QLatin1String("X"), p.x());
        parameters->insert(QLatin1String(name) + QLatin1String("Y"), p.y());
    };
    point("u_faceLeftEye", face.leftEye);
    point("u_faceRightEye", face.rightEye);
    point("u_faceNose", face.noseTip);
    point("u_faceMouth", face.mouthCenter);
    point("u_faceMouthLeft", face.mouthLeft);
    point("u_faceMouthRight", face.mouthRight);
    point("u_faceChin", face.chin);
    point("u_faceForehead", face.forehead);
    point("u_faceCenter", face.faceCenter);
    parameters->insert(QStringLiteral("u_faceRx"), face.faceRx);
    parameters->insert(QStringLiteral("u_faceRy"), face.faceRy);
    parameters->insert(QStringLiteral("u_faceAngle"), face.angle);
    parameters->insert(QStringLiteral("u_faceEyeRadius"), face.eyeRadius);
}

QString faceTrackCacheDir()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty())
        return {};
    const QString dir = QDir(root).filePath(QStringLiteral("facetracks"));
    if (!QDir().mkpath(dir))
        return {};
    return dir;
}

QString newFaceTrackPath()
{
    const QString dir = faceTrackCacheDir();
    if (dir.isEmpty())
        return {};
    const QString name = QStringLiteral("face-%1-%2.json")
                             .arg(QDateTime::currentMSecsSinceEpoch())
                             .arg(QRandomGenerator::global()->bounded(100000), 5, 10,
                                  QLatin1Char('0'));
    return QDir(dir).filePath(name);
}

bool writeFaceTrack(const QString &path, const FaceTrack &track, QString *errorOut)
{
    QJsonObject root;
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("fps")] = track.fps;
    root[QStringLiteral("startSrcUs")] = qint64(track.startSrcUs);

    QJsonArray frames;
    for (const FaceTrackFrame &frame : track.frames) {
        QJsonArray faces;
        for (const FaceAnchors &a : frame.faces)
            appendFace(&faces, a);
        frames.append(faces);
    }
    root[QStringLiteral("frames")] = frames;

    const QString partPath = path + QStringLiteral(".part");
    QFile f(partPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write %1").arg(partPath);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.close();

    QFile::remove(path);
    if (!QFile::rename(partPath, path)) {
        QFile::remove(partPath);
        if (errorOut)
            *errorOut = QStringLiteral("Could not finalize %1").arg(path);
        return false;
    }
    return true;
}

bool readFaceTrack(const QString &path, FaceTrack *out, QString *errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not read %1").arg(path);
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("Face track %1 is not valid JSON").arg(path);
        return false;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt() != kFormatVersion) {
        if (errorOut)
            *errorOut = QStringLiteral("Face track %1 has an unsupported format version").arg(path);
        return false;
    }

    out->fps = root.value(QStringLiteral("fps")).toInt();
    out->startSrcUs = TimeUs(root.value(QStringLiteral("startSrcUs")).toInteger(0));
    out->frames.clear();

    const QJsonArray frames = root.value(QStringLiteral("frames")).toArray();
    out->frames.reserve(frames.size());
    for (const QJsonValue &frameValue : frames) {
        FaceTrackFrame frame;
        const QJsonArray faces = frameValue.toArray();
        for (const QJsonValue &faceValue : faces) {
            FaceAnchors a;
            if (!readFace(faceValue.toArray(), &a)) {
                if (errorOut)
                    *errorOut = QStringLiteral("Face track %1 has a malformed entry").arg(path);
                return false;
            }
            frame.faces.append(a);
        }
        out->frames.append(frame);
    }

    if (out->fps <= 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Face track %1 has no frame rate").arg(path);
        return false;
    }
    return true;
}

std::shared_ptr<const FaceTrack> loadFaceTrackCached(const QString &path)
{
    if (path.isEmpty())
        return nullptr;

    const QFileInfo info(path);
    if (!info.exists())
        return nullptr;

    QMutexLocker lock(&g_cacheMutex);
    const auto it = g_cache.constFind(path);
    if (it != g_cache.cend() && it->modified == info.lastModified() && it->size == info.size())
        return it->track;

    auto track = std::make_shared<FaceTrack>();
    QString error;
    if (!readFaceTrack(path, track.get(), &error)) {
        // Cache the failure too: a broken sidecar must not mean a disk read on every composited
        // frame for the rest of the session.
        g_cache.insert(path, CacheEntry{info.lastModified(), info.size(), nullptr});
        return nullptr;
    }

    CacheEntry entry{info.lastModified(), info.size(), track};
    g_cache.insert(path, entry);
    return track;
}

} // namespace drift

#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QString>

#include <memory>

namespace drift {

// Everything a face warp shader needs, in normalized frame coordinates (0..1, top-left origin).
// The full 478-point mesh is deliberately not kept: the effects are radial warps around a handful
// of anchors, and a baked track of anchors is two orders of magnitude smaller than one of meshes.
struct FaceAnchors
{
    bool valid = false;

    // Iris centres. Left and right are as seen in the image, not the subject's own left and right:
    // the model's ROI is eye-aligned before landmarking, so its "left" output always lands on the
    // lower-x side of the crop.
    QPointF leftEye;
    QPointF rightEye;
    QPointF noseTip;
    QPointF mouthCenter;
    QPointF mouthLeft;
    QPointF mouthRight;
    QPointF chin;
    QPointF forehead;

    QPointF faceCenter;      // centroid of the face oval
    double faceRx = 0.0;     // half-axes of the oval, measured in the face's own rotated frame
    double faceRy = 0.0;
    double angle = 0.0;      // radians; rotation of the eye line away from horizontal
    double eyeRadius = 0.0;  // iris radius, the natural falloff scale for eye warps
    double score = 0.0;      // detector confidence, kept for tracking decisions
};

// MediaPipe's face mesh on ONNX Runtime: YuNet v2 finds faces, face_landmark_with_attention turns
// each ROI into 468 mesh points plus refined iris rings.
//
// The upstream 030_BlazeFace package the MediaPipe pipeline normally pairs with ships no ONNX
// export, so detection uses YuNet instead. Its five keypoints (eyes, nose, mouth corners) supply
// the same eye-line rotation the landmark model's ROI convention expects.
//
// All work is synchronous on the calling thread; callers run it off the GUI thread.
class FaceLandmarker
{
    struct Impl;

public:
    static FaceLandmarker &instance();

    // Loads both sessions on first use. False if the models are missing or failed to load (see
    // lastError()). Blocks for a moment — never call this from the GUI thread.
    bool available();
    QString lastError() const;

    // Cheap file-existence check that constructs no ONNX session. This is what UI gating must use.
    static bool modelPresent();

    // Detects every face in the frame, largest first, capped at maxFaces().
    //
    // `hint` is the previous frame's result. When a hinted face is still confident, its ROI is
    // derived from those anchors and the detector is skipped for it — detection is the expensive
    // and jittery half, and this is MediaPipe's own strategy. Faces are matched to the hint by
    // centre distance so a given slot keeps following the same person across a clip.
    QList<FaceAnchors> detect(const QImage &frame, const QList<FaceAnchors> *hint = nullptr);

    static int maxFaces();

    FaceLandmarker(const FaceLandmarker &) = delete;
    FaceLandmarker &operator=(const FaceLandmarker &) = delete;

private:
    FaceLandmarker();
    ~FaceLandmarker();

    std::unique_ptr<Impl> d;
};

} // namespace drift

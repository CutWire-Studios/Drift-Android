#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QString>

#include <memory>

namespace drift {

// Slices of FaceAnchors::contour. The loops a cosmetic shader needs are kept; the rest of the
// 478-point mesh still is not, because a baked track of every vertex is an order of magnitude
// larger again and nothing draws with it.
//
// Every loop is closed: the last point connects back to the first. The eyelid arcs are slices of
// the eye rings rather than separate loops, so their points are stored once.
namespace contour {

struct Span
{
    int offset;
    int count;
};

inline constexpr Span kOval{0, 36};
inline constexpr Span kLipOuter{36, 20};
inline constexpr Span kLipInner{56, 20};
inline constexpr Span kEyeLeft{76, 16};
inline constexpr Span kEyeRight{92, 16};
inline constexpr Span kBrowLeft{108, 10};
inline constexpr Span kBrowRight{118, 10};
inline constexpr int kTotalPoints = 128;

// Open arcs along the upper lids, which is where liner and shadow sit. Both eye rings are wound
// so that the first nine points run along the upper lid from inner to outer corner.
inline constexpr Span kEyeLeftUpper{76, 9};
inline constexpr Span kEyeRightUpper{92, 9};

} // namespace contour

// Everything a face shader needs, in normalized frame coordinates (0..1, top-left origin).
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

    // Contour loops, indexed by the spans above. Unlike the anchors, these are stored already in
    // width-normalized space (uv with y scaled by the frame aspect) — every shader that uses them
    // does distance maths, and pre-converting means the SDF is correct without a per-point step.
    //
    // Either empty or exactly contour::kTotalPoints long. Never partially filled.
    bool hasContours = false;
    QList<QPointF> contour;
    QPointF cheekLeft;  // uv, like the other anchors. MediaPipe has no cheek contour, so these
    QPointF cheekRight; // are centroids of four mid-cheek vertices, which are far steadier.

    // Head orientation as a unit quaternion rather than Euler angles: pitch reaches +/-90 when
    // someone looks at the floor, and three separate seam handlers in both the interpolator and
    // the smoother is more code than one nlerp.
    bool hasPose = false;
    double poseQx = 0.0;
    double poseQy = 0.0;
    double poseQz = 0.0;
    double poseQw = 1.0;
    double poseScale = 0.0; // interocular distance, width-normalized: the head's own unit of length
    double poseOx = 0.0;    // origin (eye midpoint), width-normalized 3D
    double poseOy = 0.0;
    double poseOz = 0.0;
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

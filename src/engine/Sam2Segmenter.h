#pragma once

#include <QImage>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

namespace drift {

// Where the user clicked, in source-frame pixel coordinates.
struct Sam2Prompt
{
    QVector<QPointF> points;
    QVector<int> labels; // 1 = include, 0 = exclude; parallel to points

    bool isEmpty() const { return points.isEmpty(); }
};

// Vision-encoder output for one frame. Expensive to produce, cheap to reuse: every prompt edit on
// the same frame is a decode against the same features.
struct Sam2Embedding
{
    std::vector<float> feats0;         // 1x32x256x256
    std::vector<float> feats1;         // 1x64x128x128
    std::vector<float> feats2;         // 1x256x64x64, what the memory encoder consumes
    std::vector<float> feats2NoMem;    // 1x256x64x64, conditioning-frame variant
    std::vector<float> visionPosEmbed; // 1x256x64x64
    QSize frameSize;
    bool valid = false;
};

struct Sam2Result
{
    QImage mask;               // Grayscale8 at frameSize, 0 or 255
    double maskFraction = 0.0; // covered pixels / total
    float iou = 0.0f;
    bool occluded = false;     // object_score_logits <= 0: the model cannot see the object
    bool ok = false;
    QString error;
};

// SAM2.1 video segmentation on ONNX Runtime, using the full-pipeline export from
// square-zero-labs/sam2.1-tiny-video-onnx: vision encoder, mask decoder, memory encoder, memory
// attention, and object-pointer positional encoding.
//
// Unlike image-only SAM2 exports, this carries a real memory bank across frames, so tracking is
// the model's job rather than something the caller emulates by re-prompting each frame. Occlusion
// is handled inside the graph: the mask is suppressed while the object is hidden and recovers on
// its own when it reappears.
//
// All work is synchronous on the calling thread; callers run it off the GUI thread.
class Sam2Segmenter
{
    struct Impl;

public:
    static Sam2Segmenter &instance();

    // Loads all five sessions on first use. False if the models are missing or failed to load
    // (see lastError()). Blocks for seconds — never call this from the GUI thread.
    bool available();
    QString lastError() const;

    // Cheap file-existence check: resolves the model directory without constructing any ONNX
    // session. This is what UI gating must use.
    static bool modelPresent();
    static QString installedVariant();

    QString modelVariant();

    // Runs the vision encoder. The expensive step, once per frame.
    Sam2Embedding encode(const QImage &frame);

    // Segments a frame from prompt points alone, with no memory. Milliseconds — this is what the
    // interactive window calls on every click.
    Sam2Result segmentSeed(const Sam2Embedding &embedding, const Sam2Prompt &prompt);

    // A propagation run over one clip. Seed once, then feed frames in order; the memory bank lives
    // in here. Not thread-safe: one tracker per pass.
    class Track
    {
    public:
        ~Track();

        // Establishes the object from user prompts on the reference frame. Must be called first.
        Sam2Result seed(const Sam2Embedding &embedding, const Sam2Prompt &prompt);

        // Every subsequent frame, in order. Takes no prompt: identity comes from memory.
        Sam2Result step(const Sam2Embedding &embedding);

        bool isSeeded() const;

        Track(const Track &) = delete;
        Track &operator=(const Track &) = delete;

    private:
        friend class Sam2Segmenter;
        explicit Track(Impl *impl);

        struct State;
        std::unique_ptr<State> s;
    };

    // A tracker bound to the loaded sessions, or nullptr when the model is unavailable.
    std::unique_ptr<Track> newTrack();

    Sam2Segmenter(const Sam2Segmenter &) = delete;
    Sam2Segmenter &operator=(const Sam2Segmenter &) = delete;

private:
    Sam2Segmenter();
    ~Sam2Segmenter();

    std::unique_ptr<Impl> d;
};

} // namespace drift

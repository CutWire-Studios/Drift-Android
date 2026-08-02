#pragma once

#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace drift {

// DeepFilterNet3 speech denoising on ONNX Runtime (soniqo/DeepFilterNet3-ONNX, a verified export
// of the official v0.5.6 checkpoint).
//
// The published graph is the neural network only: it takes normalized ERB and spectral features
// and returns an ERB gain mask plus complex deep-filter coefficients. The STFT, ERB banding,
// feature normalization, deep filtering and synthesis around it all live in the .cpp, and they
// have to match the reference bit-for-bit in structure or the model is fed features it was never
// trained on. See the comments there before changing any constant.
//
// All work is synchronous on the calling thread — callers run it off the GUI thread (see
// AppController::denoiseClipAudio).
class DeepFilterDenoiser
{
public:
    static DeepFilterDenoiser &instance();

    // Cheap file-existence check that constructs no ONNX session. This is what UI gating must use.
    static bool modelPresent();

    // The only rate the model works at. Callers resample to this before calling denoise().
    static int sampleRate();

    // Resolves the model directory and loads the session on first use. False if the model is
    // missing or failed to load (see lastError()). Blocks for a moment — never call from the GUI
    // thread.
    bool available();
    QString lastError() const;

    // pcm: mono float32 at sampleRate(). Returns a buffer of exactly pcm.size() samples, or an
    // empty vector when the model is unavailable or the caller cancelled.
    //
    // progress(fraction in [0,1]) returns false to request cancel.
    std::vector<float> denoise(const std::vector<float> &pcm,
                               const std::function<bool(double)> &progress);

    DeepFilterDenoiser(const DeepFilterDenoiser &) = delete;
    DeepFilterDenoiser &operator=(const DeepFilterDenoiser &) = delete;

private:
    DeepFilterDenoiser();
    ~DeepFilterDenoiser();

    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace drift

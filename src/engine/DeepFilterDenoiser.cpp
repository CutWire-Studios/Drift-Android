#include "DeepFilterDenoiser.h"

#include "GpuPackageParse.h"
#include "OrtSupport.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <onnxruntime_cxx_api.h>

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/tx.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace drift {

namespace {

// From config.json. Hard-coded rather than read blindly: the auxiliary matrices, the graph shapes
// and these numbers are one fused contract, so a config that disagrees is a broken model directory
// and not something to adapt to at runtime. loadConstants() checks config.json against them.
constexpr int kSampleRate = 48000;
constexpr int kFftSize = 960;
constexpr int kHop = 480;
constexpr int kFreqBins = 481; // kFftSize / 2 + 1
constexpr int kErbBands = 32;
constexpr int kDfBins = 96;
constexpr int kDfOrder = 5;
constexpr int kDfLookahead = 2;
constexpr float kNormAlpha = 0.99f;

// Exactly the auxiliary payload: [481,32] forward ERB, [32,481] inverse ERB, [960] Vorbis window.
constexpr qint64 kAuxFloats = kFreqBins * kErbBands + kErbBands * kFreqBins + kFftSize;
constexpr qint64 kAuxBytes = kAuxFloats * 4;

constexpr const char *kModelFile = "deepfilter.onnx";
constexpr const char *kAuxFile = "deepfilter-auxiliary.bin";
constexpr const char *kConfigFile = "config.json";

// The graph carries no recurrent-state tensors, so its GRUs restart from zero on every Run. Audio
// is therefore processed in windows that are preceded by a run-up of frames whose output is thrown
// away — by the time the window proper starts, the recurrent state has settled into the signal.
// 300 frames is 3 s, three time constants of the 0.99 feature smoothing.
constexpr int kWarmupFrames = 300;
constexpr int kWindowFrames = 2000; // 20 s of output per Run

QString resolveModelDir()
{
    const QStringList roots = GpuPackageParse::defaultSearchPaths(
        QStringLiteral("DRIFT_DENOISE_MODEL_DIR"), QStringLiteral("models/deepfilternet3"),
        QStringLiteral("denoise-model"));

    // A directory only counts as a model when every piece is there — a half-downloaded folder must
    // not look installed.
    for (const QString &root : roots) {
        const QDir dir(root);
        if (QFile::exists(dir.filePath(QLatin1String(kModelFile)))
            && QFile::exists(dir.filePath(QLatin1String(kAuxFile)))
            && QFile::exists(dir.filePath(QLatin1String(kConfigFile)))) {
            return root;
        }
    }
    return {};
}

// Number of analysis frames needed to cover `samples` once the signal has been padded front and
// back (see denoise() for why those pads exist).
int frameCountFor(qint64 paddedLength)
{
    if (paddedLength < kFftSize)
        return 1;
    return int((paddedLength - kFftSize + kHop - 1) / kHop) + 1;
}

} // namespace

struct DeepFilterDenoiser::Impl
{
    bool loaded = false;
    bool loadAttempted = false;
    QString error;
    QString modelDir;

    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inNames, outNames;

    // erbFwd[bin][band] already carries the 1/bandwidth factor, so a matrix-vector product gives
    // the mean power per band directly. erbInv[band][bin] is 1 where the bin belongs to the band,
    // i.e. a plain broadcast of the band gain back over its bins.
    std::vector<float> erbFwd; // kFreqBins * kErbBands
    std::vector<float> erbInv; // kErbBands * kFreqBins
    std::vector<float> window; // kFftSize, Vorbis

    // av_tx scratch. The forward transform is unnormalised (a unit sine comes back at kFftSize/2),
    // and forward-then-inverse multiplies by kFftSize, so analysis scales by 1/kFftSize to make the
    // pair unity. That is also what libDF does (wnorm = 1 / (fft_size^2 / (2 * hop))).
    AVTXContext *fwdTx = nullptr;
    av_tx_fn fwdFn = nullptr;
    AVTXContext *invTx = nullptr;
    av_tx_fn invFn = nullptr;
    float *timeBuf = nullptr; // kFftSize
    float *specBuf = nullptr; // interleaved complex, kFreqBins + 1

    ~Impl()
    {
        if (fwdTx)
            av_tx_uninit(&fwdTx);
        if (invTx)
            av_tx_uninit(&invTx);
        av_free(timeBuf);
        av_free(specBuf);
    }

    bool ensureLoaded();
    bool loadConstants(const QString &dir);
    bool initFft();
};

bool DeepFilterDenoiser::Impl::loadConstants(const QString &dir)
{
    QFile cfg(QDir(dir).filePath(QLatin1String(kConfigFile)));
    if (!cfg.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot read %1.").arg(QLatin1String(kConfigFile));
        return false;
    }
    const QJsonObject obj = QJsonDocument::fromJson(cfg.readAll()).object();
    const auto expect = [&](const char *key, int want) {
        const int got = obj.value(QLatin1String(key)).toInt(want);
        if (got == want)
            return true;
        error = QStringLiteral("%1 reports %2 = %3, but this build implements %4.")
                    .arg(QLatin1String(kConfigFile), QLatin1String(key))
                    .arg(got)
                    .arg(want);
        return false;
    };
    if (!expect("sample_rate", kSampleRate) || !expect("fft_size", kFftSize)
        || !expect("hop_size", kHop) || !expect("fft_bins", kFreqBins)
        || !expect("erb_bands", kErbBands) || !expect("df_bins", kDfBins)
        || !expect("df_order", kDfOrder) || !expect("df_lookahead", kDfLookahead)) {
        return false;
    }

    QFile aux(QDir(dir).filePath(QLatin1String(kAuxFile)));
    if (!aux.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot read %1.").arg(QLatin1String(kAuxFile));
        return false;
    }
    if (aux.size() != kAuxBytes) {
        error = QStringLiteral("%1 is %2 bytes; expected %3.")
                    .arg(QLatin1String(kAuxFile))
                    .arg(aux.size())
                    .arg(kAuxBytes);
        return false;
    }
    const QByteArray blob = aux.readAll();
    const auto *f = reinterpret_cast<const float *>(blob.constData());

    erbFwd.assign(f, f + kFreqBins * kErbBands);
    f += kFreqBins * kErbBands;
    erbInv.assign(f, f + kErbBands * kFreqBins);
    f += kErbBands * kFreqBins;
    window.assign(f, f + kFftSize);
    return true;
}

bool DeepFilterDenoiser::Impl::initFft()
{
    timeBuf = static_cast<float *>(av_malloc(sizeof(float) * kFftSize));
    specBuf = static_cast<float *>(av_malloc(sizeof(float) * 2 * (kFreqBins + 1)));
    if (!timeBuf || !specBuf) {
        error = QStringLiteral("Out of memory allocating FFT buffers.");
        return false;
    }

    float scale = 1.0f;
    if (av_tx_init(&fwdTx, &fwdFn, AV_TX_FLOAT_RDFT, 0, kFftSize, &scale, 0) < 0) {
        error = QStringLiteral("Failed to initialise the forward FFT (av_tx).");
        return false;
    }
    scale = 1.0f;
    if (av_tx_init(&invTx, &invFn, AV_TX_FLOAT_RDFT, 1, kFftSize, &scale, 0) < 0) {
        error = QStringLiteral("Failed to initialise the inverse FFT (av_tx).");
        return false;
    }
    return true;
}

bool DeepFilterDenoiser::Impl::ensureLoaded()
{
    if (loaded)
        return true;
    if (loadAttempted)
        return false;
    loadAttempted = true;

    if (!ort::ensureLoaded(&error))
        return false;

    modelDir = resolveModelDir();
    if (modelDir.isEmpty()) {
        error = QStringLiteral("Noise removal model not found. Install it from the Addon Manager, "
                               "place it in models/deepfilternet3, or set DRIFT_DENOISE_MODEL_DIR.");
        return false;
    }

    if (!loadConstants(modelDir))
        return false;
    if (!initFft())
        return false;

    Ort::Env &ortEnv = ort::env();
    const std::basic_string<ORTCHAR_T> path =
        ort::ortPath(QDir(modelDir).filePath(QLatin1String(kModelFile)));
    if (!ort::buildSessions(ortEnv, "denoise", false, &error, [&](Ort::SessionOptions &opts) {
            session = std::make_unique<Ort::Session>(ortEnv, path.c_str(), opts);
        })) {
        return false;
    }

    inNames = ort::sessionNames(*session, true);
    outNames = ort::sessionNames(*session, false);
    if (inNames.size() != 2 || outNames.size() != 2) {
        error = QStringLiteral("%1 has %2 inputs and %3 outputs; expected 2 and 2.")
                    .arg(QLatin1String(kModelFile))
                    .arg(inNames.size())
                    .arg(outNames.size());
        return false;
    }

    loaded = true;
    return true;
}

DeepFilterDenoiser::DeepFilterDenoiser() : d(std::make_unique<Impl>()) {}
DeepFilterDenoiser::~DeepFilterDenoiser() = default;

DeepFilterDenoiser &DeepFilterDenoiser::instance()
{
    static DeepFilterDenoiser inst;
    return inst;
}

bool DeepFilterDenoiser::modelPresent()
{
    return !resolveModelDir().isEmpty();
}

int DeepFilterDenoiser::sampleRate()
{
    return kSampleRate;
}

bool DeepFilterDenoiser::available()
{
    return d->ensureLoaded();
}

QString DeepFilterDenoiser::lastError() const
{
    return d->error;
}

std::vector<float> DeepFilterDenoiser::denoise(const std::vector<float> &pcm,
                                               const std::function<bool(double)> &progress)
{
    if (!d->ensureLoaded() || pcm.empty())
        return {};

    const auto report = [&progress](double f) { return !progress || progress(f); };

    // Two pads, both required for parity with the reference implementation:
    //  * kFftSize - kHop in front, standing in for the streaming analysis memory that libDF starts
    //    out zero-filled. This is the delay that gets sliced back off at the end.
    //  * kFftSize behind, the analysis tail the model card asks for, so the final samples are seen
    //    by a full window rather than fading out mid-frame.
    const qint64 origLen = qint64(pcm.size());
    constexpr int kFrontPad = kFftSize - kHop; // 480
    const qint64 wantLen = kFrontPad + origLen + kFftSize;
    const int totalFrames = frameCountFor(wantLen);
    const qint64 padLen = qint64(totalFrames - 1) * kHop + kFftSize;

    std::vector<float> padded(size_t(padLen), 0.0f);
    std::copy(pcm.begin(), pcm.end(), padded.begin() + kFrontPad);

    std::vector<float> synth(size_t(padLen), 0.0f);

    // Feature normalisation state, carried unbroken across every window. Only the model's GRUs
    // need the warm-up run-up; these are ours and must never restart, or each window boundary
    // would produce an audible level jump. Initial values are the reference's.
    std::vector<float> erbState(kErbBands);
    std::vector<float> specState(kDfBins);
    for (int b = 0; b < kErbBands; ++b)
        erbState[b] = -60.0f + (-90.0f - -60.0f) * float(b) / float(kErbBands - 1);
    for (int f = 0; f < kDfBins; ++f)
        specState[f] = 0.001f + (0.0001f - 0.001f) * float(f) / float(kDfBins - 1);

    // The state as it stood at the first frame of the next window's run-up, so that window can
    // replay those frames from exactly here instead of from a reset.
    std::vector<float> erbSnapshot = erbState;
    std::vector<float> specSnapshot = specState;

    std::vector<float> spec;     // interleaved complex, local frame index -> kFreqBins bins
    std::vector<float> featErb;  // [1,1,Tin,32]
    std::vector<float> featSpec; // [1,2,Tin,96]

    for (int winStart = 0; winStart < totalFrames; winStart += kWindowFrames) {
        const int winEnd = std::min(winStart + kWindowFrames, totalFrames);

        // Run-up before the window, and the deep filter's lookahead after it.
        const int from = std::max(0, winStart - kWarmupFrames);
        const int to = std::min(totalFrames, winEnd + kDfLookahead);
        const int tin = to - from;

        erbState = erbSnapshot;
        specState = specSnapshot;

        spec.assign(size_t(tin) * kFreqBins * 2, 0.0f);
        featErb.assign(size_t(tin) * kErbBands, 0.0f);
        featSpec.assign(size_t(tin) * kDfBins * 2, 0.0f);

        // The next window replays from here, so this is where its starting state is taken.
        const int snapshotAt = std::max(0, winEnd - kWarmupFrames);

        for (int t = from; t < to; ++t) {
            const int i = t - from;

            if (t == snapshotAt) {
                erbSnapshot = erbState;
                specSnapshot = specState;
            }

            for (int n = 0; n < kFftSize; ++n)
                d->timeBuf[n] = padded[size_t(t) * kHop + n] * d->window[n];
            d->fwdFn(d->fwdTx, d->specBuf, d->timeBuf, sizeof(float));

            float *frameSpec = spec.data() + size_t(i) * kFreqBins * 2;
            constexpr float kWNorm = 1.0f / float(kFftSize);
            for (int k = 0; k < kFreqBins * 2; ++k)
                frameSpec[k] = d->specBuf[k] * kWNorm;

            // ERB feature: mean power per band, to dB, then subtract an exponential running mean
            // and scale. The /40 and the 1e-10 floor are the reference's.
            float *erbOut = featErb.data() + size_t(i) * kErbBands;
            for (int b = 0; b < kErbBands; ++b) {
                float acc = 0.0f;
                for (int k = 0; k < kFreqBins; ++k) {
                    const float w = d->erbFwd[size_t(k) * kErbBands + b];
                    if (w == 0.0f)
                        continue;
                    const float re = frameSpec[2 * k];
                    const float im = frameSpec[2 * k + 1];
                    acc += w * (re * re + im * im);
                }
                const float db = 10.0f * std::log10(acc + 1e-10f);
                erbState[b] = db * (1.0f - kNormAlpha) + erbState[b] * kNormAlpha;
                erbOut[b] = (db - erbState[b]) / 40.0f;
            }

            // Spectral feature: the low bins divided by the square root of an exponential running
            // mean of their magnitude. Real and imaginary parts are separate tensor channels.
            for (int f = 0; f < kDfBins; ++f) {
                const float re = frameSpec[2 * f];
                const float im = frameSpec[2 * f + 1];
                const float mag = std::hypot(re, im);
                specState[f] = mag * (1.0f - kNormAlpha) + specState[f] * kNormAlpha;
                const float norm = std::sqrt(std::max(specState[f], 1e-12f));
                featSpec[(size_t(0) * tin + i) * kDfBins + f] = re / norm;
                featSpec[(size_t(1) * tin + i) * kDfBins + f] = im / norm;
            }
        }

        if (!report(double(winStart) / double(totalFrames)))
            return {};

        // One Run over run-up + window + lookahead.
        const std::array<int64_t, 4> erbShape{1, 1, tin, kErbBands};
        const std::array<int64_t, 4> specShape{1, 2, tin, kDfBins};
        std::vector<Ort::Value> inputs;
        inputs.emplace_back(Ort::Value::CreateTensor<float>(ort::cpuMemory(), featErb.data(), featErb.size(),
                                                            erbShape.data(), erbShape.size()));
        inputs.emplace_back(Ort::Value::CreateTensor<float>(
            ort::cpuMemory(), featSpec.data(), featSpec.size(), specShape.data(), specShape.size()));

        std::vector<Ort::Value> outputs;
        try {
            const std::vector<const char *> inN = ort::cstrs(d->inNames);
            const std::vector<const char *> outN = ort::cstrs(d->outNames);
            outputs = d->session->Run(Ort::RunOptions{nullptr}, inN.data(), inputs.data(),
                                      inputs.size(), outN.data(), outN.size());
        } catch (const Ort::Exception &e) {
            d->error = QString::fromUtf8(e.what());
            qWarning() << "[denoise] inference failed:" << d->error;
            return {};
        }

        const float *erbMask = outputs[0].GetTensorData<float>();
        const float *dfCoefs = outputs[1].GetTensorData<float>();

        // Deep filtering reads the untouched noisy spectrum, so the masked result is written into
        // a separate buffer rather than over the top of `spec`.
        std::vector<float> outSpec(size_t(kFreqBins) * 2, 0.0f);

        for (int t = winStart; t < winEnd; ++t) {
            const int i = t - from;
            const float *frameSpec = spec.data() + size_t(i) * kFreqBins * 2;

            // Broadcast the 32 band gains back over their bins and apply.
            for (int k = 0; k < kFreqBins; ++k) {
                float gain = 0.0f;
                for (int b = 0; b < kErbBands; ++b)
                    gain += d->erbInv[size_t(b) * kFreqBins + k] * erbMask[size_t(i) * kErbBands + b];
                outSpec[2 * k] = frameSpec[2 * k] * gain;
                outSpec[2 * k + 1] = frameSpec[2 * k + 1] * gain;
            }

            // Deep filter over the low bins: a complex FIR across frames, replacing the masked
            // values entirely. Tap j reads frame (t + j - lookahead), so the filter spans two
            // frames either side of the current one.
            for (int f = 0; f < kDfBins; ++f) {
                float re = 0.0f;
                float im = 0.0f;
                for (int j = 0; j < kDfOrder; ++j) {
                    const int src = i + j - kDfLookahead;
                    if (src < 0 || src >= tin)
                        continue;
                    const float *s = spec.data() + size_t(src) * kFreqBins * 2;
                    const size_t c = ((size_t(j) * tin + i) * kDfBins + f) * 2;
                    const float cr = dfCoefs[c];
                    const float ci = dfCoefs[c + 1];
                    re += s[2 * f] * cr - s[2 * f + 1] * ci;
                    im += s[2 * f] * ci + s[2 * f + 1] * cr;
                }
                outSpec[2 * f] = re;
                outSpec[2 * f + 1] = im;
            }

            // Inverse transform, window again, overlap-add. av_tx's inverse RDFT destroys its
            // input, hence the copy into the scratch buffer.
            std::memcpy(d->specBuf, outSpec.data(), sizeof(float) * size_t(kFreqBins) * 2);
            d->invFn(d->invTx, d->timeBuf, d->specBuf, sizeof(float));

            float *dst = synth.data() + size_t(t) * kHop;
            for (int n = 0; n < kFftSize; ++n)
                dst[n] += d->timeBuf[n] * d->window[n];
        }

        if (!report(double(winEnd) / double(totalFrames)))
            return {};
    }

    // Drop the analysis delay introduced by the front pad and trim back to the caller's length.
    return std::vector<float>(synth.begin() + kFrontPad, synth.begin() + kFrontPad + origLen);
}

} // namespace drift

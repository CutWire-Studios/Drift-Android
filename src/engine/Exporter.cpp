#include "Exporter.h"

#include "AudioMixer.h"
#include "FrameCompositor.h"
#include "core/Project.h"
#include "core/Time.h"

#include <QFile>
#include <QImage>

#ifdef Q_OS_ANDROID
#include "AndroidUri.h"

#include <QDir>
#include <QFileInfo>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtCore/qcoreapplication_platform.h>

#include <atomic>
#endif

#include <climits>
#include <cmath>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

namespace {

#ifdef Q_OS_ANDROID

constexpr const char *kExportServiceClass = "org/cutwire/drift/ExportService";

std::atomic<int> g_backgroundHolds{0};
std::atomic<int> g_notifiedPercent{-1};

void startExportService(const QString &title)
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return;

    QJniEnvironment env;
    jclass clazz = env.findClass(kExportServiceClass);
    if (!clazz)
        return;

    // The titled overload arrives with the Java side; until it does, every job borrows the
    // notification's hardcoded "Exporting" — wrong for a denoise, but it still holds the process in
    // the foreground. Probing is what makes the fallback work at all: QJniObject clears the
    // NoSuchMethodError itself and turns the missing method into a silent no-op, so calling and
    // checking afterwards would report success and start nothing.
    if (jmethodID titled = env.findStaticMethod(
            clazz, "start", "(Landroid/content/Context;Ljava/lang/String;)V")) {
        QJniObject::callStaticMethod<void>(clazz, titled, context.object(),
                                           QJniObject::fromString(title).object<jstring>());
    } else {
        QJniObject::callStaticMethod<void>(clazz, "start", "(Landroid/content/Context;)V",
                                           context.object());
    }
    env.checkAndClearExceptions();
}

void stopExportService()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kExportServiceClass, "stop", "(Landroid/content/Context;)V",
                                       context.object());
    // A job that cannot raise its notification still has to run: the JNI failure is the service
    // being unavailable, not the render being wrong.
    QJniEnvironment().checkAndClearExceptions();
}

#endif

enum class RateMode { Crf, Bitrate, Lossless };

struct VideoCodecDef {
    const char *id;
    const char *label;
    // Preferred encoder names, tried in order (nullptr-terminated).
    const char *const *encoderNames;
    AVPixelFormat pixFmt;
    RateMode rateMode;
    bool supportsPreset;
    const char *const *presets; // nullptr-terminated; nullptr if none
    const char *defaultPreset;
    int defaultCrf;
    // "mp4" | "webm" | "mkv" preferred when paired with a friendly audio codec.
    const char *preferredContainer;
};

struct AudioCodecDef {
    const char *id;
    const char *label;
    const char *const *encoderNames;
    bool lossless;
    // "mp4" | "webm" | "mkv" | "any"
    const char *containerFamily;
};

const char *const kLibx264[] = {"libx264", "h264", nullptr};
const char *const kLibx265[] = {"libx265", "hevc", nullptr};
const char *const kLibSvtAv1[] = {"libsvtav1", nullptr};
const char *const kFfv1[] = {"ffv1", nullptr};
const char *const kMpeg4[] = {"mpeg4", nullptr};
const char *const kMpeg2[] = {"mpeg2video", nullptr};
const char *const kLibvpx[] = {"libvpx", nullptr};
const char *const kLibvpxVp9[] = {"libvpx-vp9", nullptr};
const char *const kDnxhd[] = {"dnxhd", nullptr};
const char *const kProres[] = {"prores_ks", "prores", nullptr};
const char *const kLibtheora[] = {"libtheora", nullptr};

const char *const kX264Presets[] = {"ultrafast", "superfast", "veryfast", "faster", "fast",
                                    "medium",    "slow",      "slower",   "veryslow", nullptr};
const char *const kSvtPresets[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", nullptr};
const char *const kVp9CpuUsed[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", nullptr};

const VideoCodecDef kVideoCodecs[] = {
    {"av1_svt", "AV1 (SVT)", kLibSvtAv1, AV_PIX_FMT_YUV420P, RateMode::Crf, true, kSvtPresets, "6", 35, "mp4"},
    {"av1_svt_10", "AV1 10-bit (SVT)", kLibSvtAv1, AV_PIX_FMT_YUV420P10LE, RateMode::Crf, true, kSvtPresets, "6", 35,
     "mkv"},
    {"ffv1", "FFV1", kFfv1, AV_PIX_FMT_YUV444P, RateMode::Lossless, false, nullptr, nullptr, 0, "mkv"},
    {"h264", "H.264 (x264)", kLibx264, AV_PIX_FMT_YUV420P, RateMode::Crf, true, kX264Presets, "medium", 18, "mp4"},
    {"h264_10", "H.264 10-bit (x264)", kLibx264, AV_PIX_FMT_YUV420P10LE, RateMode::Crf, true, kX264Presets, "medium",
     18, "mkv"},
    {"h265", "H.265 (x265)", kLibx265, AV_PIX_FMT_YUV420P, RateMode::Crf, true, kX264Presets, "medium", 28, "mp4"},
    {"h265_10", "H.265 10-bit (x265)", kLibx265, AV_PIX_FMT_YUV420P10LE, RateMode::Crf, true, kX264Presets, "medium",
     28, "mkv"},
    {"h265_12", "H.265 12-bit (x265)", kLibx265, AV_PIX_FMT_YUV420P12LE, RateMode::Crf, true, kX264Presets, "medium",
     28, "mkv"},
    {"mpeg4", "MPEG-4", kMpeg4, AV_PIX_FMT_YUV420P, RateMode::Bitrate, false, nullptr, nullptr, 0, "mp4"},
    {"mpeg2", "MPEG-2", kMpeg2, AV_PIX_FMT_YUV420P, RateMode::Bitrate, false, nullptr, nullptr, 0, "mkv"},
    {"vp8", "VP8", kLibvpx, AV_PIX_FMT_YUV420P, RateMode::Crf, true, kVp9CpuUsed, "4", 10, "webm"},
    {"vp9", "VP9", kLibvpxVp9, AV_PIX_FMT_YUV420P, RateMode::Crf, true, kVp9CpuUsed, "4", 32, "webm"},
    {"vp9_10", "VP9 10-bit", kLibvpxVp9, AV_PIX_FMT_YUV420P10LE, RateMode::Crf, true, kVp9CpuUsed, "4", 32, "webm"},
    {"dnxhr", "DNxHR", kDnxhd, AV_PIX_FMT_YUV422P, RateMode::Bitrate, false, nullptr, nullptr, 0, "mkv"},
    {"dnxhr_10", "DNxHR 10-bit", kDnxhd, AV_PIX_FMT_YUV422P10LE, RateMode::Bitrate, false, nullptr, nullptr, 0,
     "mkv"},
    {"prores", "ProRes", kProres, AV_PIX_FMT_YUV422P10LE, RateMode::Lossless, false, nullptr, nullptr, 0, "mkv"},
    {"theora", "Theora", kLibtheora, AV_PIX_FMT_YUV420P, RateMode::Bitrate, false, nullptr, nullptr, 0, "mkv"},
};

const char *const kAacEnc[] = {"aac", nullptr};
const char *const kOpusEnc[] = {"libopus", "opus", nullptr};
const char *const kMp3Enc[] = {"libmp3lame", "mp3", nullptr};
const char *const kAc3Enc[] = {"ac3", nullptr};
const char *const kFlacEnc[] = {"flac", nullptr};

const AudioCodecDef kAudioCodecs[] = {
    {"aac", "AAC", kAacEnc, false, "mp4"},
    {"opus", "Opus", kOpusEnc, false, "webm"},
    {"mp3", "MP3", kMp3Enc, false, "mp4"},
    {"ac3", "AC3", kAc3Enc, false, "mp4"},
    {"flac", "FLAC", kFlacEnc, true, "mkv"},
};

const AVCodec *findEncoder(const char *const *names)
{
    for (int i = 0; names && names[i]; ++i) {
        if (const AVCodec *c = avcodec_find_encoder_by_name(names[i]))
            return c;
    }
    return nullptr;
}

const VideoCodecDef *findVideoDef(const QString &id)
{
    for (const VideoCodecDef &def : kVideoCodecs) {
        if (id == QLatin1String(def.id))
            return &def;
    }
    return nullptr;
}

const AudioCodecDef *findAudioDef(const QString &id)
{
    for (const AudioCodecDef &def : kAudioCodecs) {
        if (id == QLatin1String(def.id))
            return &def;
    }
    return nullptr;
}

QStringList presetsToList(const char *const *presets)
{
    QStringList out;
    for (int i = 0; presets && presets[i]; ++i)
        out.append(QString::fromUtf8(presets[i]));
    return out;
}

QVariantMap videoDefToMap(const VideoCodecDef &def)
{
    const AVCodec *enc = findEncoder(def.encoderNames);
    QVariantMap m;
    m.insert(QStringLiteral("id"), QString::fromUtf8(def.id));
    m.insert(QStringLiteral("label"), QString::fromUtf8(def.label));
    m.insert(QStringLiteral("available"), enc != nullptr);
    m.insert(QStringLiteral("supportsCrf"), def.rateMode == RateMode::Crf);
    m.insert(QStringLiteral("supportsBitrate"), def.rateMode != RateMode::Lossless);
    m.insert(QStringLiteral("lossless"), def.rateMode == RateMode::Lossless);
    m.insert(QStringLiteral("supportsPreset"), def.supportsPreset);
    m.insert(QStringLiteral("presets"), presetsToList(def.presets));
    m.insert(QStringLiteral("defaultPreset"),
             def.defaultPreset ? QString::fromUtf8(def.defaultPreset) : QString());
    m.insert(QStringLiteral("defaultCrf"), def.defaultCrf);
    m.insert(QStringLiteral("container"), QString::fromUtf8(def.preferredContainer));
    return m;
}

QVariantMap audioDefToMap(const AudioCodecDef &def)
{
    const AVCodec *enc = findEncoder(def.encoderNames);
    QVariantMap m;
    m.insert(QStringLiteral("id"), QString::fromUtf8(def.id));
    m.insert(QStringLiteral("label"), QString::fromUtf8(def.label));
    m.insert(QStringLiteral("available"), enc != nullptr);
    m.insert(QStringLiteral("lossless"), def.lossless);
    m.insert(QStringLiteral("container"), QString::fromUtf8(def.containerFamily));
    return m;
}

void evenDims(int &w, int &h)
{
    w &= ~1;
    h &= ~1;
    w = qMax(2, w);
    h = qMax(2, h);
}

void scaleSize(int projW, int projH, int targetH, int &outW, int &outH)
{
    outH = targetH > 0 ? targetH : projH;
    outW = static_cast<int>(std::llround(static_cast<double>(projW) * outH / projH));
    evenDims(outW, outH);
}

// Sends a frame (or nullptr to flush) to the encoder and interleaves the packets.
bool encodeWriteFrame(AVFormatContext *fmt, AVCodecContext *codec, AVStream *stream, AVFrame *frame,
                      AVPacket *pkt, QString *errorOut)
{
    int rc = avcodec_send_frame(codec, frame);
    if (rc < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Encoder rejected a frame");
        return false;
    }

    while (rc >= 0) {
        rc = avcodec_receive_packet(codec, pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            break;
        if (rc < 0) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to read an encoded packet");
            return false;
        }
        av_packet_rescale_ts(pkt, codec->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        rc = av_interleaved_write_frame(fmt, pkt);
        av_packet_unref(pkt);
        if (rc < 0) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to write a packet");
            return false;
        }
    }
    return true;
}

AVSampleFormat pickSampleFmt(const AVCodec *codec)
{
    const void *configs = nullptr;
    if (!codec
        || avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &configs,
                                        nullptr)
            < 0
        || !configs)
        return AV_SAMPLE_FMT_FLTP;

    const auto *fmts = static_cast<const AVSampleFormat *>(configs);
    // Prefer planar float, then planar s16, then first listed.
    for (const AVSampleFormat *p = fmts; *p != AV_SAMPLE_FMT_NONE; ++p) {
        if (*p == AV_SAMPLE_FMT_FLTP)
            return *p;
    }
    for (const AVSampleFormat *p = fmts; *p != AV_SAMPLE_FMT_NONE; ++p) {
        if (*p == AV_SAMPLE_FMT_S16P)
            return *p;
    }
    return fmts[0];
}

void fillAudioFrame(AVFrame *frame, AVSampleFormat fmt, const float *interleaved, int samples)
{
    if (fmt == AV_SAMPLE_FMT_FLTP) {
        auto *left = reinterpret_cast<float *>(frame->data[0]);
        auto *right = reinterpret_cast<float *>(frame->data[1]);
        for (int i = 0; i < samples; ++i) {
            left[i] = interleaved[i * 2];
            right[i] = interleaved[i * 2 + 1];
        }
        return;
    }
    if (fmt == AV_SAMPLE_FMT_FLT) {
        auto *dst = reinterpret_cast<float *>(frame->data[0]);
        std::memcpy(dst, interleaved, static_cast<size_t>(samples) * 2 * sizeof(float));
        return;
    }
    if (fmt == AV_SAMPLE_FMT_S16P) {
        auto *left = reinterpret_cast<int16_t *>(frame->data[0]);
        auto *right = reinterpret_cast<int16_t *>(frame->data[1]);
        for (int i = 0; i < samples; ++i) {
            left[i] = static_cast<int16_t>(qBound(-1.0f, interleaved[i * 2], 1.0f) * 32767.0f);
            right[i] = static_cast<int16_t>(qBound(-1.0f, interleaved[i * 2 + 1], 1.0f) * 32767.0f);
        }
        return;
    }
    if (fmt == AV_SAMPLE_FMT_S16) {
        auto *dst = reinterpret_cast<int16_t *>(frame->data[0]);
        for (int i = 0; i < samples * 2; ++i)
            dst[i] = static_cast<int16_t>(qBound(-1.0f, interleaved[i], 1.0f) * 32767.0f);
        return;
    }
    // Fallback: treat as planar float.
    auto *left = reinterpret_cast<float *>(frame->data[0]);
    auto *right = reinterpret_cast<float *>(frame->data[1]);
    for (int i = 0; i < samples; ++i) {
        left[i] = interleaved[i * 2];
        right[i] = interleaved[i * 2 + 1];
    }
}

void applyVideoRateControl(AVCodecContext *vctx, const VideoCodecDef &def, const ExportSettings &settings)
{
    const QString id = QString::fromUtf8(def.id);
    const bool useCrf = settings.rateControl == QLatin1String("crf") && def.rateMode == RateMode::Crf;
    const bool useBitrate =
        settings.rateControl == QLatin1String("bitrate") && def.rateMode != RateMode::Lossless;

    if (def.rateMode == RateMode::Lossless)
        return;

    if (useCrf) {
        const int crf = settings.crf;
        if (id.startsWith(QLatin1String("av1"))) {
            // SVT-AV1 uses crf via private option; also set bit_rate 0.
            vctx->bit_rate = 0;
            av_opt_set_int(vctx->priv_data, "crf", crf, 0);
        } else if (id.startsWith(QLatin1String("vp8")) || id.startsWith(QLatin1String("vp9"))) {
            vctx->bit_rate = 0;
            av_opt_set_int(vctx->priv_data, "crf", crf, 0);
            av_opt_set_int(vctx->priv_data, "b", 0, 0);
        } else {
            // x264 / x265
            vctx->bit_rate = 0;
            av_opt_set(vctx->priv_data, "crf", QByteArray::number(crf).constData(), 0);
        }
        return;
    }

    if (useBitrate || def.rateMode == RateMode::Bitrate) {
        vctx->bit_rate = static_cast<int64_t>(qMax(100, settings.videoBitrateKbps)) * 1000;
    }
}

void applyVideoPreset(AVCodecContext *vctx, const VideoCodecDef &def, const ExportSettings &settings)
{
    if (!def.supportsPreset || !vctx->priv_data)
        return;
    QByteArray preset = settings.videoPreset.toUtf8();
    if (preset.isEmpty() && def.defaultPreset)
        preset = def.defaultPreset;

    const QString id = QString::fromUtf8(def.id);
    if (id.startsWith(QLatin1String("vp8")) || id.startsWith(QLatin1String("vp9"))) {
        av_opt_set(vctx->priv_data, "cpu-used", preset.constData(), 0);
        av_opt_set(vctx->priv_data, "deadline", "good", 0);
        return;
    }
    if (id.startsWith(QLatin1String("av1"))) {
        // SVT-AV1 preset is an integer 0–12.
        av_opt_set(vctx->priv_data, "preset", preset.constData(), 0);
        return;
    }
    av_opt_set(vctx->priv_data, "preset", preset.constData(), 0);
}

// libav muxer name for an audio-only container id (may differ from the file extension).
QByteArray audioOnlyMuxerName(const QString &container)
{
    if (container == QLatin1String("m4a"))
        return QByteArrayLiteral("ipod");
    return container.toUtf8();
}

// Drift's SDR pipeline is BT.709 limited-range end-to-end (GPU NV12 decode + export).
void applySdrBt709Tags(AVCodecContext *vctx)
{
    if (!vctx)
        return;
    vctx->color_range = AVCOL_RANGE_MPEG;
    vctx->colorspace = AVCOL_SPC_BT709;
    vctx->color_primaries = AVCOL_PRI_BT709;
    vctx->color_trc = AVCOL_TRC_BT709;
}

void applySdrBt709Tags(AVFrame *frame)
{
    if (!frame)
        return;
    frame->color_range = AVCOL_RANGE_MPEG;
    frame->colorspace = AVCOL_SPC_BT709;
    frame->color_primaries = AVCOL_PRI_BT709;
    frame->color_trc = AVCOL_TRC_BT709;
}

// RGB (full) → YUV (limited) with BT.709 coefficients. Without this, swscale
// defaults to BT.601 and players that assume BT.709 for HD shift the hues.
bool configureExportSws(SwsContext *sws)
{
    if (!sws)
        return false;
    const int *coeff = sws_getCoefficients(SWS_CS_ITU709);
    // brightness=0, contrast=1<<16, saturation=1<<16 are identity.
    return sws_setColorspaceDetails(sws, coeff, 1 /* src full-range RGB */, coeff,
                                    0 /* dst limited-range YUV */, 0, 1 << 16, 1 << 16)
           >= 0;
}

void applyExportMetadata(AVFormatContext *fmt, const ExportSettings &settings)
{
    if (!fmt || !settings.audioOnly)
        return;
    auto setIf = [fmt](const char *key, const QString &value) {
        if (value.isEmpty())
            return;
        const QByteArray utf8 = value.toUtf8();
        av_dict_set(&fmt->metadata, key, utf8.constData(), 0);
    };
    setIf("title", settings.metadataTitle);
    setIf("artist", settings.metadataArtist);
    setIf("album", settings.metadataAlbum);
    setIf("comment", settings.metadataComment);
}

bool runAudioOnlyExport(const drift::Project &project, const ExportSettings &settings, const QString &outputPath,
                        QString *errorOut, const Exporter::ProgressFn &onProgress)
{
    const AudioCodecDef *adef = findAudioDef(settings.audioCodecId);
    if (!adef) {
        if (errorOut)
            *errorOut = QStringLiteral("Unknown audio codec selection");
        return false;
    }

    const AVCodec *acodec = findEncoder(adef->encoderNames);
    if (!acodec) {
        if (errorOut)
            *errorOut = QStringLiteral("Selected audio encoder is not available");
        return false;
    }

    const int sampleRate = project.sampleRate() > 0 ? project.sampleRate() : 48000;
    const drift::TimeUs durationUs = project.durationUs();
    if (durationUs <= 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Timeline is empty");
        return false;
    }

    const int64_t totalAudioSamples =
        qMax<int64_t>(0, std::llround(static_cast<double>(durationUs) * sampleRate / 1e6));

    Exporter::BackgroundHold hold(QStringLiteral("Exporting"));

    AVFormatContext *fmt = nullptr;
    AVCodecContext *actx = nullptr;
    AVStream *astream = nullptr;
    AVFrame *aframe = nullptr;
    AVPacket *pkt = nullptr;
    bool ok = false;
    bool cancelled = false;
    bool headerWritten = false;
    QString error;

    const QByteArray outUtf8 = outputPath.toUtf8();
    avformat_alloc_output_context2(&fmt, nullptr, nullptr, outUtf8.constData());
    if (!fmt) {
        const QString container = Exporter::preferredAudioOnlyContainer(settings.audioCodecId);
        const QByteArray muxer = audioOnlyMuxerName(container);
        avformat_alloc_output_context2(&fmt, nullptr, muxer.constData(), outUtf8.constData());
    }
    if (!fmt) {
        error = QStringLiteral("Could not determine output format");
        goto cleanup;
    }

    {
        astream = avformat_new_stream(fmt, nullptr);
        if (!astream) {
            error = QStringLiteral("Could not create output stream");
            goto cleanup;
        }

        actx = avcodec_alloc_context3(acodec);
        if (!actx) {
            error = QStringLiteral("Could not allocate audio encoder");
            goto cleanup;
        }

        const AVSampleFormat audioFmt = pickSampleFmt(acodec);
        actx->sample_fmt = audioFmt;
        actx->sample_rate = sampleRate;
        av_channel_layout_default(&actx->ch_layout, 2);
        if (!adef->lossless)
            actx->bit_rate = static_cast<int64_t>(qMax(32, settings.audioBitrateKbps)) * 1000;
        actx->time_base = AVRational{1, sampleRate};
        if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
            actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(actx, acodec, nullptr) < 0) {
            error = QStringLiteral("Could not open the audio encoder");
            goto cleanup;
        }

        avcodec_parameters_from_context(astream->codecpar, actx);
        astream->time_base = actx->time_base;

        const int frameSize = actx->frame_size > 0 ? actx->frame_size : 1024;

        if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&fmt->pb, outUtf8.constData(), AVIO_FLAG_WRITE) < 0) {
                error = QStringLiteral("Could not open the output file");
                goto cleanup;
            }
        }

        applyExportMetadata(fmt, settings);

        if (avformat_write_header(fmt, nullptr) < 0) {
            error = QStringLiteral("Could not write the file header");
            goto cleanup;
        }
        headerWritten = true;

        pkt = av_packet_alloc();
        aframe = av_frame_alloc();
        if (!pkt || !aframe) {
            error = QStringLiteral("Out of memory");
            goto cleanup;
        }

        aframe->format = audioFmt;
        aframe->sample_rate = sampleRate;
        av_channel_layout_copy(&aframe->ch_layout, &actx->ch_layout);
        aframe->nb_samples = frameSize;
        if (av_frame_get_buffer(aframe, 0) < 0) {
            error = QStringLiteral("Could not allocate the audio frame");
            goto cleanup;
        }

        AudioMixer mixer;
        mixer.setProject(&project);

        std::vector<float> audioBuffer;
        int64_t audioSamplesGenerated = 0;
        int64_t audioPts = 0;

        auto flushAudioFrames = [&](bool drainAll) -> bool {
            while (static_cast<int64_t>(audioBuffer.size()) >= static_cast<int64_t>(frameSize) * 2) {
                if (av_frame_make_writable(aframe) < 0) {
                    error = QStringLiteral("Audio frame not writable");
                    return false;
                }
                aframe->nb_samples = frameSize;
                fillAudioFrame(aframe, audioFmt, audioBuffer.data(), frameSize);
                aframe->pts = audioPts;
                audioPts += frameSize;
                if (!encodeWriteFrame(fmt, actx, astream, aframe, pkt, &error))
                    return false;
                audioBuffer.erase(audioBuffer.begin(), audioBuffer.begin() + frameSize * 2);
            }
            if (drainAll && !audioBuffer.empty()) {
                const int remaining = static_cast<int>(audioBuffer.size() / 2);
                if (av_frame_make_writable(aframe) < 0) {
                    error = QStringLiteral("Audio frame not writable");
                    return false;
                }
                aframe->nb_samples = remaining;
                fillAudioFrame(aframe, audioFmt, audioBuffer.data(), remaining);
                aframe->pts = audioPts;
                audioPts += remaining;
                if (!encodeWriteFrame(fmt, actx, astream, aframe, pkt, &error))
                    return false;
                audioBuffer.clear();
            }
            return true;
        };

        while (audioSamplesGenerated < totalAudioSamples) {
            const double fraction = static_cast<double>(audioSamplesGenerated) / totalAudioSamples;
            Exporter::BackgroundHold::setPercent(static_cast<int>(fraction * 100.0));
            if (onProgress && totalAudioSamples > 0 && !onProgress(fraction)) {
                cancelled = true;
                break;
            }

            const int64_t chunkSamples = qMin<int64_t>(frameSize * 8, totalAudioSamples - audioSamplesGenerated);
            const size_t base = audioBuffer.size();
            audioBuffer.resize(base + static_cast<size_t>(chunkSamples) * 2);
            const drift::TimeUs audioStartUs = static_cast<drift::TimeUs>(
                (audioSamplesGenerated * drift::kUsPerSecond) / sampleRate);
            mixer.mix(audioStartUs, static_cast<int>(chunkSamples), sampleRate, audioBuffer.data() + base);
            audioSamplesGenerated += chunkSamples;
            if (!flushAudioFrames(false))
                goto cleanup;
        }

        if (!cancelled) {
            if (!flushAudioFrames(true))
                goto cleanup;
            if (!encodeWriteFrame(fmt, actx, astream, nullptr, pkt, &error))
                goto cleanup;
            if (av_write_trailer(fmt) < 0) {
                error = QStringLiteral("Could not finalize the file");
                goto cleanup;
            }
            ok = true;
        }
    }

cleanup:
    if (aframe)
        av_frame_free(&aframe);
    if (pkt)
        av_packet_free(&pkt);
    if (actx)
        avcodec_free_context(&actx);
    if (fmt) {
        if (headerWritten && !ok && fmt->pb)
            av_write_trailer(fmt);
        if (fmt->pb && !(fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmt->pb);
        avformat_free_context(fmt);
    }

    if (!ok && QFile::exists(outputPath))
        QFile::remove(outputPath);

    if (!ok && errorOut) {
        *errorOut = cancelled ? QStringLiteral("Export cancelled")
                              : (error.isEmpty() ? QStringLiteral("Export failed") : error);
    }
    return ok;
}

#ifdef Q_OS_ANDROID
// avio has no content:// protocol, and a document created by ACTION_CREATE_DOCUMENT has no path to
// open instead — so the encode runs into app storage and the result is streamed into the document
// afterwards. The staging name carries the suffix from the document's display name because that is
// the only place the container the user asked for is still legible: the URI itself has none, and
// avformat picks the muxer by extension.
bool runToDocument(const drift::Project &project, const ExportSettings &settings, const QUrl &target,
                   QString *errorOut, const Exporter::ProgressFn &onProgress)
{
    QString suffix = QFileInfo(AndroidUri::displayName(target)).suffix();
    if (suffix.isEmpty()) {
        suffix = settings.audioOnly
                     ? Exporter::defaultSuffix(
                           Exporter::preferredAudioOnlyContainer(settings.audioCodecId), true)
                     : Exporter::defaultSuffix(
                           Exporter::preferredContainer(settings.videoCodecId, settings.audioCodecId),
                           false);
    }

    QTemporaryFile staging(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                               .filePath(QStringLiteral("export-XXXXXX.") + suffix));
    if (!staging.open()) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not stage the export");
        return false;
    }
    const QString stagingPath = staging.fileName();
    staging.close();

    if (!Exporter::run(project, settings, stagingPath, errorOut, onProgress))
        return false;

    QFile encoded(stagingPath);
    std::unique_ptr<QFile> sink = AndroidUri::openForWrite(target);
    if (!sink || !encoded.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write to the chosen location");
        return false;
    }

    std::vector<char> buffer(1 << 16);
    while (!encoded.atEnd()) {
        const qint64 read = encoded.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (read <= 0 || sink->write(buffer.data(), read) != read) {
            if (errorOut)
                *errorOut = QStringLiteral("Could not write to the chosen location");
            return false;
        }
    }
    if (!sink->flush()) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write to the chosen location");
        return false;
    }
    return true;
}
#endif

} // namespace

Exporter::BackgroundHold::BackgroundHold(const QString &title)
{
#ifdef Q_OS_ANDROID
    if (g_backgroundHolds.fetch_add(1, std::memory_order_acq_rel) == 0) {
        g_notifiedPercent.store(-1, std::memory_order_relaxed);
        startExportService(title);
    }
    drift::android::acquireKeepScreenOn();
#else
    Q_UNUSED(title);
#endif
}

Exporter::BackgroundHold::~BackgroundHold()
{
#ifdef Q_OS_ANDROID
    drift::android::releaseKeepScreenOn();
    if (g_backgroundHolds.fetch_sub(1, std::memory_order_acq_rel) == 1)
        stopExportService();
#endif
}

void Exporter::BackgroundHold::setPercent(int percent)
{
#ifdef Q_OS_ANDROID
    percent = qBound(0, percent, 100);
    if (g_notifiedPercent.exchange(percent, std::memory_order_relaxed) == percent)
        return;

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kExportServiceClass, "setPercent",
                                       "(Landroid/content/Context;I)V", context.object(), percent);
    QJniEnvironment().checkAndClearExceptions();
#else
    Q_UNUSED(percent);
#endif
}

const QList<ExportScalePreset> &Exporter::scalePresets()
{
    static const QList<ExportScalePreset> kPresets = {
        {QStringLiteral("source"), QStringLiteral("Same as project"), 0, 16000},
        {QStringLiteral("1080p"), QStringLiteral("1080p"), 1080, 12000},
        {QStringLiteral("720p"), QStringLiteral("720p"), 720, 8000},
        {QStringLiteral("480p"), QStringLiteral("480p"), 480, 4000},
    };
    return kPresets;
}

const ExportScalePreset *Exporter::scalePresetById(const QString &id)
{
    for (const ExportScalePreset &preset : scalePresets()) {
        if (preset.id == id)
            return &preset;
    }
    return nullptr;
}

QVariantList Exporter::videoCodecs()
{
    QVariantList out;
    for (const VideoCodecDef &def : kVideoCodecs)
        out.append(videoDefToMap(def));
    return out;
}

QVariantList Exporter::audioCodecs()
{
    QVariantList out;
    for (const AudioCodecDef &def : kAudioCodecs)
        out.append(audioDefToMap(def));
    return out;
}

QVariantMap Exporter::videoCodecById(const QString &id)
{
    if (const VideoCodecDef *def = findVideoDef(id))
        return videoDefToMap(*def);
    return {};
}

QVariantMap Exporter::audioCodecById(const QString &id)
{
    if (const AudioCodecDef *def = findAudioDef(id))
        return audioDefToMap(*def);
    return {};
}

QString Exporter::preferredContainer(const QString &videoCodecId, const QString &audioCodecId)
{
    const VideoCodecDef *vdef = findVideoDef(videoCodecId);
    const AudioCodecDef *adef = findAudioDef(audioCodecId);
    const QString vCont = vdef ? QString::fromUtf8(vdef->preferredContainer) : QStringLiteral("mkv");
    const QString aFam = adef ? QString::fromUtf8(adef->containerFamily) : QStringLiteral("mkv");

    // Lossless / awkward video always prefers mkv.
    if (vCont == QLatin1String("mkv"))
        return QStringLiteral("mkv");

    // WebM video with Opus (or another webm-family audio) → webm; else mkv.
    if (vCont == QLatin1String("webm")) {
        if (aFam == QLatin1String("webm") || audioCodecId == QLatin1String("opus")
            || audioCodecId == QLatin1String("vorbis"))
            return QStringLiteral("webm");
        return QStringLiteral("mkv");
    }

    // MP4-friendly video: need MP4-friendly audio.
    if (vCont == QLatin1String("mp4")) {
        if (aFam == QLatin1String("mp4"))
            return QStringLiteral("mp4");
        if (aFam == QLatin1String("webm"))
            return QStringLiteral("mkv"); // e.g. H.264 + Opus
        return QStringLiteral("mkv");
    }

    return QStringLiteral("mkv");
}

QString Exporter::preferredAudioOnlyContainer(const QString &audioCodecId)
{
    if (audioCodecId == QLatin1String("aac"))
        return QStringLiteral("m4a");
    if (audioCodecId == QLatin1String("mp3"))
        return QStringLiteral("mp3");
    if (audioCodecId == QLatin1String("opus"))
        return QStringLiteral("opus");
    if (audioCodecId == QLatin1String("ac3"))
        return QStringLiteral("ac3");
    if (audioCodecId == QLatin1String("flac"))
        return QStringLiteral("flac");
    return QStringLiteral("m4a");
}

QStringList Exporter::saveFilters(const QString &container, bool audioOnly)
{
    if (audioOnly) {
        if (container == QLatin1String("m4a"))
            return {QStringLiteral("AAC audio (*.m4a)")};
        if (container == QLatin1String("mp3"))
            return {QStringLiteral("MP3 audio (*.mp3)")};
        if (container == QLatin1String("opus"))
            return {QStringLiteral("Opus audio (*.opus)")};
        if (container == QLatin1String("ac3"))
            return {QStringLiteral("AC3 audio (*.ac3)")};
        if (container == QLatin1String("flac"))
            return {QStringLiteral("FLAC audio (*.flac)")};
        return {QStringLiteral("Audio files (*.*)")};
    }
    if (container == QLatin1String("webm"))
        return {QStringLiteral("WebM video (*.webm)")};
    if (container == QLatin1String("mkv"))
        return {QStringLiteral("Matroska video (*.mkv)"), QStringLiteral("MP4 video (*.mp4)")};
    return {QStringLiteral("MP4 video (*.mp4)"), QStringLiteral("Matroska video (*.mkv)")};
}

QString Exporter::defaultSuffix(const QString &container, bool audioOnly)
{
    if (audioOnly) {
        if (container == QLatin1String("m4a"))
            return QStringLiteral("m4a");
        if (container == QLatin1String("mp3"))
            return QStringLiteral("mp3");
        if (container == QLatin1String("opus"))
            return QStringLiteral("opus");
        if (container == QLatin1String("ac3"))
            return QStringLiteral("ac3");
        if (container == QLatin1String("flac"))
            return QStringLiteral("flac");
        return QStringLiteral("m4a");
    }
    if (container == QLatin1String("webm"))
        return QStringLiteral("webm");
    if (container == QLatin1String("mkv"))
        return QStringLiteral("mkv");
    return QStringLiteral("mp4");
}

QVariantList Exporter::scaleOptions(int projectWidth, int projectHeight)
{
    QVariantList out;
    if (projectWidth <= 0 || projectHeight <= 0)
        return out;

    for (const ExportScalePreset &preset : scalePresets()) {
        // Skip downscale targets that would upscale.
        if (preset.targetHeight > 0 && projectHeight <= preset.targetHeight)
            continue;

        int outW = 0;
        int outH = 0;
        scaleSize(projectWidth, projectHeight, preset.targetHeight, outW, outH);

        const QString label = QStringLiteral("%1 · %2×%3").arg(preset.label).arg(outW).arg(outH);

        out.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("label"), label},
            {QStringLiteral("targetHeight"), preset.targetHeight},
            {QStringLiteral("width"), outW},
            {QStringLiteral("height"), outH},
            {QStringLiteral("videoBitrateKbps"), preset.videoBitrateKbps},
        });
    }

    // Always include source if somehow filtered out (shouldn't happen).
    if (out.isEmpty()) {
        int outW = 0;
        int outH = 0;
        scaleSize(projectWidth, projectHeight, 0, outW, outH);
        out.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("source")},
            {QStringLiteral("label"), QStringLiteral("Same as project · %1×%2").arg(outW).arg(outH)},
            {QStringLiteral("targetHeight"), 0},
            {QStringLiteral("width"), outW},
            {QStringLiteral("height"), outH},
            {QStringLiteral("videoBitrateKbps"), 16000},
        });
    }
    return out;
}

QVariantList Exporter::frameRateOptions(int projectFps)
{
    struct FrameRatePreset
    {
        const char *id;
        int num;
        int den;
        const char *label;
    };

    // NTSC rates carry a 1001 denominator; an integer fps could not express them,
    // which is why ExportSettings keeps the rate rational.
    static const FrameRatePreset presets[] = {
        {"23.976", 24000, 1001, "23.976 fps (NTSC film)"},
        {"24", 24, 1, "24 fps"},
        {"25", 25, 1, "25 fps (PAL)"},
        {"29.97", 30000, 1001, "29.97 fps (NTSC)"},
        {"30", 30, 1, "30 fps"},
        {"48", 48, 1, "48 fps"},
        {"50", 50, 1, "50 fps"},
        {"59.94", 60000, 1001, "59.94 fps (NTSC)"},
        {"60", 60, 1, "60 fps"},
        {"120", 120, 1, "120 fps"},
        {"240", 240, 1, "240 fps"},
    };

    QVariantList out;
    // 0/1 means "whatever the project is set to", so the export follows later
    // project-setup changes instead of pinning the rate at dialog time.
    out.append(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("project")},
        {QStringLiteral("label"), QStringLiteral("Same as project (%1 fps)").arg(qMax(1, projectFps))},
        {QStringLiteral("fpsNum"), 0},
        {QStringLiteral("fpsDen"), 1},
    });
    for (const FrameRatePreset &preset : presets) {
        out.append(QVariantMap{
            {QStringLiteral("id"), QString::fromLatin1(preset.id)},
            {QStringLiteral("label"), QString::fromLatin1(preset.label)},
            {QStringLiteral("fpsNum"), preset.num},
            {QStringLiteral("fpsDen"), preset.den},
        });
    }
    return out;
}

ExportSettings Exporter::defaultSettings()
{
    ExportSettings s;
    // Prefer first available CRF codec starting at h264.
    const QStringList prefer = {QStringLiteral("h264"), QStringLiteral("h265"), QStringLiteral("vp9"),
                                QStringLiteral("av1_svt")};
    for (const QString &id : prefer) {
        const QVariantMap m = videoCodecById(id);
        if (m.value(QStringLiteral("available")).toBool()) {
            s.videoCodecId = id;
            s.crf = m.value(QStringLiteral("defaultCrf"), 18).toInt();
            s.videoPreset = m.value(QStringLiteral("defaultPreset")).toString();
            if (s.videoPreset.isEmpty())
                s.videoPreset = QStringLiteral("medium");
            break;
        }
    }
    if (audioCodecById(QStringLiteral("aac")).value(QStringLiteral("available")).toBool())
        s.audioCodecId = QStringLiteral("aac");
    else {
        for (const QVariant &v : audioCodecs()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("available")).toBool()) {
                s.audioCodecId = m.value(QStringLiteral("id")).toString();
                break;
            }
        }
    }
    s.rateControl = QStringLiteral("crf");
    return s;
}

ExportSettings Exporter::settingsFromMap(const QVariantMap &map)
{
    ExportSettings s = defaultSettings();
    if (map.contains(QStringLiteral("targetHeight")))
        s.targetHeight = map.value(QStringLiteral("targetHeight")).toInt();
    if (map.contains(QStringLiteral("fpsNum")))
        s.fpsNum = map.value(QStringLiteral("fpsNum")).toInt();
    if (map.contains(QStringLiteral("fpsDen")))
        s.fpsDen = map.value(QStringLiteral("fpsDen")).toInt();
    // Anything nonsensical falls back to the project rate rather than producing a
    // broken time_base the muxer would reject.
    if (s.fpsNum <= 0 || s.fpsDen <= 0) {
        s.fpsNum = 0;
        s.fpsDen = 1;
    } else if (static_cast<int64_t>(s.fpsNum) > static_cast<int64_t>(kMaxExportFps) * s.fpsDen) {
        s.fpsNum = kMaxExportFps;
        s.fpsDen = 1;
    }
    if (map.contains(QStringLiteral("videoCodecId")))
        s.videoCodecId = map.value(QStringLiteral("videoCodecId")).toString();
    if (map.contains(QStringLiteral("rateControl")))
        s.rateControl = map.value(QStringLiteral("rateControl")).toString();
    if (map.contains(QStringLiteral("crf")))
        s.crf = map.value(QStringLiteral("crf")).toInt();
    if (map.contains(QStringLiteral("videoBitrateKbps")))
        s.videoBitrateKbps = map.value(QStringLiteral("videoBitrateKbps")).toInt();
    if (map.contains(QStringLiteral("videoPreset")))
        s.videoPreset = map.value(QStringLiteral("videoPreset")).toString();
    if (map.contains(QStringLiteral("audioCodecId")))
        s.audioCodecId = map.value(QStringLiteral("audioCodecId")).toString();
    if (map.contains(QStringLiteral("audioBitrateKbps")))
        s.audioBitrateKbps = map.value(QStringLiteral("audioBitrateKbps")).toInt();
    if (map.contains(QStringLiteral("audioOnly")))
        s.audioOnly = map.value(QStringLiteral("audioOnly")).toBool();
    if (map.contains(QStringLiteral("metadataTitle")))
        s.metadataTitle = map.value(QStringLiteral("metadataTitle")).toString();
    if (map.contains(QStringLiteral("metadataArtist")))
        s.metadataArtist = map.value(QStringLiteral("metadataArtist")).toString();
    if (map.contains(QStringLiteral("metadataAlbum")))
        s.metadataAlbum = map.value(QStringLiteral("metadataAlbum")).toString();
    if (map.contains(QStringLiteral("metadataComment")))
        s.metadataComment = map.value(QStringLiteral("metadataComment")).toString();
    return s;
}

bool Exporter::run(const drift::Project &project, const ExportSettings &settings, const QString &outputPath,
                   QString *errorOut, const ProgressFn &onProgress)
{
#ifdef Q_OS_ANDROID
    if (const QUrl target(outputPath); AndroidUri::isContentUri(target))
        return runToDocument(project, settings, target, errorOut, onProgress);
#endif

    if (settings.audioOnly)
        return runAudioOnlyExport(project, settings, outputPath, errorOut, onProgress);

    const int projW = project.width();
    const int projH = project.height();
    if (projW <= 0 || projH <= 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Invalid project resolution");
        return false;
    }

    const VideoCodecDef *vdef = findVideoDef(settings.videoCodecId);
    const AudioCodecDef *adef = findAudioDef(settings.audioCodecId);
    if (!vdef || !adef) {
        if (errorOut)
            *errorOut = QStringLiteral("Unknown codec selection");
        return false;
    }

    const AVCodec *vcodec = findEncoder(vdef->encoderNames);
    const AVCodec *acodec = findEncoder(adef->encoderNames);
    if (!vcodec || !acodec) {
        if (errorOut)
            *errorOut = QStringLiteral("Selected encoder is not available");
        return false;
    }

    int outW = 0;
    int outH = 0;
    scaleSize(projW, projH, settings.targetHeight, outW, outH);

    // The export rate is independent of the project rate: sampling the timeline
    // faster pulls extra frames out of the source wherever it has them, which is
    // what makes slowed clips look smooth.
    AVRational frameRate{qMax(1, project.fps()), 1};
    if (settings.fpsNum > 0 && settings.fpsDen > 0)
        frameRate = AVRational{settings.fpsNum, settings.fpsDen};
    av_reduce(&frameRate.num, &frameRate.den, frameRate.num, frameRate.den, INT_MAX);
    const AVRational frameTb{frameRate.den, frameRate.num};
    const double fpsValue = av_q2d(frameRate);

    const int sampleRate = project.sampleRate() > 0 ? project.sampleRate() : 48000;
    const drift::TimeUs durationUs = project.durationUs();
    if (durationUs <= 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Timeline is empty");
        return false;
    }

    const int64_t totalFrames =
        qMax<int64_t>(1, std::llround(static_cast<double>(durationUs) * fpsValue / 1e6));
    const int64_t totalAudioSamples =
        qMax<int64_t>(0, std::llround(static_cast<double>(durationUs) * sampleRate / 1e6));

    BackgroundHold hold(QStringLiteral("Exporting"));

    AVFormatContext *fmt = nullptr;
    AVCodecContext *vctx = nullptr;
    AVCodecContext *actx = nullptr;
    AVStream *vstream = nullptr;
    AVStream *astream = nullptr;
    AVFrame *vframe = nullptr;
    AVFrame *aframe = nullptr;
    AVPacket *pkt = nullptr;
    SwsContext *sws = nullptr;
    bool ok = false;
    bool cancelled = false;
    bool headerWritten = false;
    QString error;

    // Encoded straight into the chosen path: a file handed over by the documents portal cannot be
    // renamed or replaced, so writing a sibling ".part" and moving it into place would strand the
    // result next to the file the user picked.
    const QByteArray outUtf8 = outputPath.toUtf8();

    avformat_alloc_output_context2(&fmt, nullptr, nullptr, outUtf8.constData());
    if (!fmt) {
        // Portal pickers hand back whatever name the user typed, extension or not; when there is
        // nothing to guess from, fall back to the container this codec pair asks for.
        const QString container = preferredContainer(settings.videoCodecId, settings.audioCodecId);
        const QByteArray muxer = container == QLatin1String("mkv")
                                     ? QByteArrayLiteral("matroska")
                                     : container.toUtf8();
        avformat_alloc_output_context2(&fmt, nullptr, muxer.constData(), outUtf8.constData());
    }
    if (!fmt) {
        error = QStringLiteral("Could not determine output format");
        goto cleanup;
    }

    {
        vstream = avformat_new_stream(fmt, nullptr);
        astream = avformat_new_stream(fmt, nullptr);
        if (!vstream || !astream) {
            error = QStringLiteral("Could not create output streams");
            goto cleanup;
        }

        vctx = avcodec_alloc_context3(vcodec);
        actx = avcodec_alloc_context3(acodec);
        if (!vctx || !actx) {
            error = QStringLiteral("Could not allocate encoders");
            goto cleanup;
        }

        vctx->width = outW;
        vctx->height = outH;
        vctx->pix_fmt = vdef->pixFmt;
        vctx->time_base = frameTb;
        vctx->framerate = frameRate;
        vctx->gop_size = qMax(1, static_cast<int>(std::llround(fpsValue * 2.0)));
        vctx->max_b_frames = 2;
        applySdrBt709Tags(vctx);
        if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
            vctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        applyVideoRateControl(vctx, *vdef, settings);
        applyVideoPreset(vctx, *vdef, settings);

        // Some encoders (esp. hardware fallbacks) may not accept the preferred
        // pix_fmt; fall back to the first supported format if open fails later.
        const AVSampleFormat audioFmt = pickSampleFmt(acodec);
        actx->sample_fmt = audioFmt;
        actx->sample_rate = sampleRate;
        av_channel_layout_default(&actx->ch_layout, 2);
        if (!adef->lossless)
            actx->bit_rate = static_cast<int64_t>(qMax(32, settings.audioBitrateKbps)) * 1000;
        actx->time_base = AVRational{1, sampleRate};
        if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
            actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(vctx, vcodec, nullptr) < 0) {
            error = QStringLiteral("Could not open the video encoder");
            goto cleanup;
        }
        if (avcodec_open2(actx, acodec, nullptr) < 0) {
            error = QStringLiteral("Could not open the audio encoder");
            goto cleanup;
        }

        avcodec_parameters_from_context(vstream->codecpar, vctx);
        avcodec_parameters_from_context(astream->codecpar, actx);
        vstream->time_base = vctx->time_base;
        astream->time_base = actx->time_base;

        const int frameSize = actx->frame_size > 0 ? actx->frame_size : 1024;
        const AVPixelFormat outPixFmt = vctx->pix_fmt;

        if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&fmt->pb, outUtf8.constData(), AVIO_FLAG_WRITE) < 0) {
                error = QStringLiteral("Could not open the output file");
                goto cleanup;
            }
        }

        if (avformat_write_header(fmt, nullptr) < 0) {
            error = QStringLiteral("Could not write the file header");
            goto cleanup;
        }
        headerWritten = true;

        // Lanczos when down/upscaling; bicubic is enough for a pure format convert.
        const int swsFlags = (projW != outW || projH != outH) ? SWS_LANCZOS : SWS_BICUBIC;
        sws = sws_getContext(projW, projH, AV_PIX_FMT_RGBA, outW, outH, outPixFmt, swsFlags, nullptr,
                             nullptr, nullptr);
        if (!sws) {
            error = QStringLiteral("Could not create the scaler");
            goto cleanup;
        }
        if (!configureExportSws(sws)) {
            error = QStringLiteral("Could not configure export colour conversion");
            goto cleanup;
        }

        pkt = av_packet_alloc();
        vframe = av_frame_alloc();
        aframe = av_frame_alloc();
        if (!pkt || !vframe || !aframe) {
            error = QStringLiteral("Out of memory");
            goto cleanup;
        }

        vframe->format = outPixFmt;
        vframe->width = outW;
        vframe->height = outH;
        if (av_frame_get_buffer(vframe, 32) < 0) {
            error = QStringLiteral("Could not allocate the video frame");
            goto cleanup;
        }

        aframe->format = audioFmt;
        aframe->sample_rate = sampleRate;
        av_channel_layout_copy(&aframe->ch_layout, &actx->ch_layout);
        aframe->nb_samples = frameSize;
        if (av_frame_get_buffer(aframe, 0) < 0) {
            error = QStringLiteral("Could not allocate the audio frame");
            goto cleanup;
        }

        FrameCompositor compositor;
        compositor.setProject(&project);
        AudioMixer mixer;
        mixer.setProject(&project);

        std::vector<float> audioBuffer;
        int64_t audioSamplesGenerated = 0;
        int64_t audioPts = 0;

        auto flushAudioFrames = [&](bool drainAll) -> bool {
            while (static_cast<int64_t>(audioBuffer.size()) >= static_cast<int64_t>(frameSize) * 2) {
                if (av_frame_make_writable(aframe) < 0) {
                    error = QStringLiteral("Audio frame not writable");
                    return false;
                }
                aframe->nb_samples = frameSize;
                fillAudioFrame(aframe, audioFmt, audioBuffer.data(), frameSize);
                aframe->pts = audioPts;
                audioPts += frameSize;
                if (!encodeWriteFrame(fmt, actx, astream, aframe, pkt, &error))
                    return false;
                audioBuffer.erase(audioBuffer.begin(), audioBuffer.begin() + frameSize * 2);
            }
            if (drainAll && !audioBuffer.empty()) {
                const int remaining = static_cast<int>(audioBuffer.size() / 2);
                if (av_frame_make_writable(aframe) < 0) {
                    error = QStringLiteral("Audio frame not writable");
                    return false;
                }
                aframe->nb_samples = remaining;
                fillAudioFrame(aframe, audioFmt, audioBuffer.data(), remaining);
                aframe->pts = audioPts;
                audioPts += remaining;
                if (!encodeWriteFrame(fmt, actx, astream, aframe, pkt, &error))
                    return false;
                audioBuffer.clear();
            }
            return true;
        };

        for (int64_t i = 0; i < totalFrames; ++i) {
            const double fraction = static_cast<double>(i) / totalFrames;
            BackgroundHold::setPercent(static_cast<int>(fraction * 100.0));
            if (onProgress && !onProgress(fraction)) {
                cancelled = true;
                break;
            }

            const drift::TimeUs t = static_cast<drift::TimeUs>(
                std::llround(static_cast<double>(i) * 1e6 * frameRate.den / frameRate.num));
            QImage img = compositor.compositeAt(t);
            if (img.isNull()) {
                img = QImage(projW, projH, QImage::Format_RGBA8888);
                img.fill(Qt::black);
            } else if (img.format() != QImage::Format_RGBA8888) {
                img = img.convertToFormat(QImage::Format_RGBA8888);
            }
            if (img.width() != projW || img.height() != projH)
                img = img.scaled(projW, projH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

            if (av_frame_make_writable(vframe) < 0) {
                error = QStringLiteral("Video frame not writable");
                goto cleanup;
            }
            {
                const uint8_t *srcData[4] = {img.constBits(), nullptr, nullptr, nullptr};
                const int srcStride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
                sws_scale(sws, srcData, srcStride, 0, projH, vframe->data, vframe->linesize);
            }
            applySdrBt709Tags(vframe);
            vframe->pts = i;
            if (!encodeWriteFrame(fmt, vctx, vstream, vframe, pkt, &error))
                goto cleanup;

            // Integer math keeps A/V in exact lockstep even on 1001-denominator rates.
            const int64_t targetSamples =
                qMin(totalAudioSamples,
                     ((i + 1) * static_cast<int64_t>(sampleRate) * frameRate.den) / frameRate.num);
            const int need = static_cast<int>(targetSamples - audioSamplesGenerated);
            if (need > 0) {
                const size_t base = audioBuffer.size();
                audioBuffer.resize(base + static_cast<size_t>(need) * 2);
                const drift::TimeUs audioStartUs = static_cast<drift::TimeUs>(
                    (static_cast<int64_t>(audioSamplesGenerated) * drift::kUsPerSecond) / sampleRate);
                mixer.mix(audioStartUs, need, sampleRate, audioBuffer.data() + base);
                audioSamplesGenerated = targetSamples;
            }
            if (!flushAudioFrames(false))
                goto cleanup;
        }

        if (!cancelled) {
            if (!flushAudioFrames(true))
                goto cleanup;
            if (!encodeWriteFrame(fmt, vctx, vstream, nullptr, pkt, &error))
                goto cleanup;
            if (!encodeWriteFrame(fmt, actx, astream, nullptr, pkt, &error))
                goto cleanup;
            if (av_write_trailer(fmt) < 0) {
                error = QStringLiteral("Could not finalize the file");
                goto cleanup;
            }
            ok = true;
        }
    }

cleanup:
    if (sws)
        sws_freeContext(sws);
    if (vframe)
        av_frame_free(&vframe);
    if (aframe)
        av_frame_free(&aframe);
    if (pkt)
        av_packet_free(&pkt);
    if (vctx)
        avcodec_free_context(&vctx);
    if (actx)
        avcodec_free_context(&actx);
    if (fmt) {
        if (headerWritten && !ok && fmt->pb)
            av_write_trailer(fmt);
        if (fmt->pb && !(fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmt->pb);
        avformat_free_context(fmt);
    }

    // A failed or cancelled run leaves a partial file at the destination. Best effort: the portal
    // may refuse the unlink, in which case the half-written file stays and the caller reports the
    // failure anyway.
    if (!ok && QFile::exists(outputPath))
        QFile::remove(outputPath);

    if (!ok && errorOut) {
        *errorOut = cancelled ? QStringLiteral("Export cancelled")
                              : (error.isEmpty() ? QStringLiteral("Export failed") : error);
    }
    return ok;
}

QUrl Exporter::publishToGallery(const QUrl &source, const QString &displayName, QString *errorOut)
{
#ifdef Q_OS_ANDROID
    // The scoped insert below (RELATIVE_PATH + IS_PENDING) is API 29. Publishing on 28 would mean
    // WRITE_EXTERNAL_STORAGE and a raw path into the shared volume — a permission this app
    // deliberately never asks for, for one release of Android.
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 29) {
        if (errorOut)
            *errorOut = QStringLiteral("Saving to the gallery needs Android 10 or newer");
        return {};
    }

    std::unique_ptr<QFile> encoded = AndroidUri::openForRead(source);
    if (!encoded) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not read the exported file");
        return {};
    }

    const QString mimeType =
        QMimeDatabase().mimeTypeForFile(displayName, QMimeDatabase::MatchExtension).name();
    const bool audio = mimeType.startsWith(QLatin1String("audio/"));

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject resolver =
        context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
    QJniObject collection = QJniObject::getStaticObjectField(
        audio ? "android/provider/MediaStore$Audio$Media" : "android/provider/MediaStore$Video$Media",
        "EXTERNAL_CONTENT_URI", "Landroid/net/Uri;");
    if (!resolver.isValid() || !collection.isValid()) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not reach the media library");
        return {};
    }

    const auto putString = [](QJniObject &values, const char *key, const QString &value) {
        values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/String;)V",
                                QJniObject::fromString(QString::fromLatin1(key)).object<jstring>(),
                                QJniObject::fromString(value).object<jstring>());
    };
    const auto putInt = [](QJniObject &values, const char *key, jint value) {
        values.callMethod<void>(
            "put", "(Ljava/lang/String;Ljava/lang/Integer;)V",
            QJniObject::fromString(QString::fromLatin1(key)).object<jstring>(),
            QJniObject::callStaticObjectMethod("java/lang/Integer", "valueOf",
                                               "(I)Ljava/lang/Integer;", value)
                .object());
    };

    QJniObject values("android/content/ContentValues");
    putString(values, "_display_name", displayName);
    putString(values, "mime_type", mimeType);
    putString(values, "relative_path", audio ? QStringLiteral("Music/Drift")
                                             : QStringLiteral("Movies/Drift"));
    // Pending until the bytes are there, so the gallery never shows a half-written video.
    putInt(values, "is_pending", 1);

    QJniObject item = resolver.callObjectMethod(
        "insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;",
        collection.object(), values.object());
    QJniEnvironment env;
    env.checkAndClearExceptions();
    if (!item.isValid()) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not add the export to the media library");
        return {};
    }

    // Written through the resolver's own stream rather than QFile: Qt's content file engine gates
    // on Context.checkUriPermission, which reports nothing for a MediaStore row this app owns
    // outright and has been granted no explicit URI permission for.
    bool ok = false;
    QJniObject stream = resolver.callObjectMethod(
        "openOutputStream", "(Landroid/net/Uri;)Ljava/io/OutputStream;", item.object());
    if (!env.checkAndClearExceptions() && stream.isValid()) {
        constexpr jsize kChunk = 1 << 16;
        jbyteArray chunk = env->NewByteArray(kChunk);
        std::vector<char> buffer(kChunk);
        ok = true;
        while (!encoded->atEnd()) {
            const qint64 read = encoded->read(buffer.data(), kChunk);
            if (read <= 0) {
                ok = false;
                break;
            }
            env->SetByteArrayRegion(chunk, 0, static_cast<jsize>(read),
                                    reinterpret_cast<const jbyte *>(buffer.data()));
            stream.callMethod<void>("write", "([BII)V", chunk, jint(0), static_cast<jint>(read));
            if (env.checkAndClearExceptions()) {
                ok = false;
                break;
            }
        }
        env->DeleteLocalRef(chunk);
        stream.callMethod<void>("close", "()V");
        if (env.checkAndClearExceptions())
            ok = false;
    }

    if (ok) {
        QJniObject done("android/content/ContentValues");
        putInt(done, "is_pending", 0);
        resolver.callMethod<jint>("update",
                                  "(Landroid/net/Uri;Landroid/content/ContentValues;"
                                  "Ljava/lang/String;[Ljava/lang/String;)I",
                                  item.object(), done.object(), static_cast<jstring>(nullptr),
                                  static_cast<jobjectArray>(nullptr));
        env.checkAndClearExceptions();
        return QUrl(item.toString());
    }

    resolver.callMethod<jint>("delete", "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I",
                              item.object(), static_cast<jstring>(nullptr),
                              static_cast<jobjectArray>(nullptr));
    env.checkAndClearExceptions();
    if (errorOut)
        *errorOut = QStringLiteral("Could not write the export to the media library");
    return {};
#else
    Q_UNUSED(source);
    Q_UNUSED(displayName);
    Q_UNUSED(errorOut);
    return {};
#endif
}

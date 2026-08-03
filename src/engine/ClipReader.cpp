#include "ClipReader.h"

#include <QThread>
#include <QtMath>

#include <cmath>
#include <cstring>
#include <limits>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {

bool isHardwarePixelFormat(AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

int swsColorspaceFromFrame(const AVFrame *frame)
{
    if (!frame)
        return SWS_CS_ITU709;
    switch (frame->colorspace) {
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        return SWS_CS_ITU601;
    case AVCOL_SPC_SMPTE240M:
        return SWS_CS_SMPTE240M;
    case AVCOL_SPC_FCC:
        return SWS_CS_FCC;
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return SWS_CS_ITU709;
    case AVCOL_SPC_UNSPECIFIED:
    default:
        // Drift's SDR pipeline defaults to BT.709 when the bitstream is untagged.
        return SWS_CS_ITU709;
    }
}

// YUV (typically limited) → RGB/NV12 with source colourspace when tagged.
void configureDecodeSws(SwsContext *sws, const AVFrame *src, int dstRange)
{
    if (!sws || !src)
        return;
    const int *coeff = sws_getCoefficients(swsColorspaceFromFrame(src));
    // Unspecified range is treated as limited (MPEG/TV) — the common case for camera footage.
    const int srcRange = src->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    sws_setColorspaceDetails(sws, coeff, srcRange, coeff, dstRange, 0, 1 << 16, 1 << 16);
}

int swsFlagsForResize(int srcW, int srcH, int dstW, int dstH)
{
    return (srcW != dstW || srcH != dstH) ? SWS_LANCZOS : SWS_BICUBIC;
}

// Prefer the VAAPI surface format when the decoder offers it; otherwise pick the
// first software format so get_format never hard-fails with AV_PIX_FMT_NONE
// (that path leaves the hwaccel decoder in a half-initialized state).
AVPixelFormat hwGetFormat(AVCodecContext *ctx, const AVPixelFormat *pixFmts)
{
    const AVPixelFormat prefer =
        ctx && ctx->opaque ? *static_cast<const AVPixelFormat *>(ctx->opaque) : AV_PIX_FMT_NONE;

    for (const AVPixelFormat *p = pixFmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == prefer)
            return *p;
    }

    for (const AVPixelFormat *p = pixFmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (!isHardwarePixelFormat(*p))
            return *p;
    }

    return pixFmts ? pixFmts[0] : AV_PIX_FMT_NONE;
}

QImage frameToRgba(const AVFrame *frame, SwsContext *&sws, int targetWidth, int targetHeight)
{
    if (!frame || targetWidth <= 0 || targetHeight <= 0)
        return {};
    if (isHardwarePixelFormat(static_cast<AVPixelFormat>(frame->format)))
        return {};

    const int flags = swsFlagsForResize(frame->width, frame->height, targetWidth, targetHeight);
    sws = sws_getCachedContext(sws, frame->width, frame->height,
                               static_cast<AVPixelFormat>(frame->format), targetWidth, targetHeight,
                               AV_PIX_FMT_RGBA, flags, nullptr, nullptr, nullptr);
    if (!sws)
        return {};
    configureDecodeSws(sws, frame, 1 /* full-range RGB */);

    AVFrame *rgba = av_frame_alloc();
    if (!rgba)
        return {};

    rgba->format = AV_PIX_FMT_RGBA;
    rgba->width = targetWidth;
    rgba->height = targetHeight;
    if (av_frame_get_buffer(rgba, 0) < 0) {
        av_frame_free(&rgba);
        return {};
    }

    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, rgba->data, rgba->linesize);

    QImage image(rgba->data[0], targetWidth, targetHeight, rgba->linesize[0], QImage::Format_RGBA8888);
    const QImage copy = image.copy();
    av_frame_free(&rgba);
    return copy;
}

// Pack an NV12 AVFrame into the flat Y-then-UV buffer the compositor uploads.
Nv12Frame packNv12(const AVFrame *nv12, int targetWidth, int targetHeight)
{
    Nv12Frame out;
    const qsizetype yBytes = qsizetype(targetWidth) * targetHeight;
    const qsizetype uvBytes = qsizetype(targetWidth) * (targetHeight / 2);
    out.data.resize(yBytes + uvBytes);
    // Copy plane-by-plane in case linesize > width.
    for (int y = 0; y < targetHeight; ++y) {
        memcpy(out.data.data() + qsizetype(y) * targetWidth, nv12->data[0] + y * nv12->linesize[0],
               size_t(targetWidth));
    }
    for (int y = 0; y < targetHeight / 2; ++y) {
        memcpy(out.data.data() + yBytes + qsizetype(y) * targetWidth,
               nv12->data[1] + y * nv12->linesize[1], size_t(targetWidth));
    }
    out.width = targetWidth;
    out.height = targetHeight;
    return out;
}

Nv12Frame frameToNv12(const AVFrame *frame, SwsContext *&sws, int targetWidth, int targetHeight)
{
    Nv12Frame out;
    if (!frame || targetWidth <= 0 || targetHeight <= 0)
        return out;
    if (isHardwarePixelFormat(static_cast<AVPixelFormat>(frame->format)))
        return out;

    // NV12 requires even dimensions.
    targetWidth &= ~1;
    targetHeight &= ~1;
    if (targetWidth < 2 || targetHeight < 2)
        return out;

    // The VAAPI VPP path already produced NV12 at exactly this size — packing it
    // directly skips a full-frame scale that would only be a copy.
    if (frame->format == AV_PIX_FMT_NV12 && frame->width == targetWidth
        && frame->height == targetHeight) {
        return packNv12(frame, targetWidth, targetHeight);
    }

    const int flags = swsFlagsForResize(frame->width, frame->height, targetWidth, targetHeight);
    sws = sws_getCachedContext(sws, frame->width, frame->height,
                               static_cast<AVPixelFormat>(frame->format), targetWidth, targetHeight,
                               AV_PIX_FMT_NV12, flags, nullptr, nullptr, nullptr);
    if (!sws)
        return out;
    // Keep limited-range YUV so GlRuntime's TV-range BT.709 shader expands correctly.
    configureDecodeSws(sws, frame, 0 /* limited-range NV12 */);

    AVFrame *nv12 = av_frame_alloc();
    if (!nv12)
        return out;

    nv12->format = AV_PIX_FMT_NV12;
    nv12->width = targetWidth;
    nv12->height = targetHeight;
    if (av_frame_get_buffer(nv12, 0) < 0) {
        av_frame_free(&nv12);
        return out;
    }

    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, nv12->data, nv12->linesize);

    out = packNv12(nv12, targetWidth, targetHeight);
    av_frame_free(&nv12);
    return out;
}

drift::TimeUs ptsToUs(const AVFrame *frame, const AVRational &timeBase)
{
    if (!frame || frame->pts == AV_NOPTS_VALUE)
        return 0;
    return av_rescale_q(frame->pts, timeBase, {1, drift::kUsPerSecond});
}

} // namespace

ClipReader::ClipReader() = default;

ClipReader::~ClipReader()
{
    close();
}

void ClipReader::teardownVideoDecoder()
{
    teardownHwScaler();
    if (m_sws) {
        sws_freeContext(m_sws);
        m_sws = nullptr;
    }
    if (m_swsNv12) {
        sws_freeContext(m_swsNv12);
        m_swsNv12 = nullptr;
    }
    if (m_videoCtx)
        avcodec_free_context(&m_videoCtx);
    if (m_hwDeviceCtx)
        av_buffer_unref(&m_hwDeviceCtx);
    m_hwAccelActive = false;
    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_videoPositioned = false;
    m_lastVideoPtsUs = 0;
    m_decodeW = 0;
    m_decodeH = 0;
    m_videoCache.clear();
    m_nv12Cache.clear();
}

QSize ClipReader::decodeSizeFor(int maxWidth, int maxHeight) const
{
    const AVCodecParameters *par = m_fmt->streams[m_videoStream]->codecpar;
    const int srcW = par->width;
    const int srcH = par->height;
    if (srcW <= 0 || srcH <= 0)
        return {qMax(1, maxWidth), qMax(1, maxHeight)};
    if (maxWidth <= 0 || maxHeight <= 0)
        return {srcW, srcH};

    // Never decode larger than the source; scaling up is the compositor's job.
    const double fit = qMin(static_cast<double>(maxWidth) / srcW, static_cast<double>(maxHeight) / srcH);
    if (fit >= 1.0)
        return {srcW, srcH};

    // Quantize up to 1/8 steps. A preview panel dragged a few pixels wider must
    // not change the decode size, or every resize would drop the frame cache.
    const double quantized = qMin(1.0, std::ceil(fit * 8.0) / 8.0);
    const int w = qMax(2, static_cast<int>(std::lround(srcW * quantized)) & ~1);
    const int h = qMax(2, static_cast<int>(std::lround(srcH * quantized)) & ~1);
    return {w, h};
}

void ClipReader::applyDecodeSize(const QSize &size)
{
    if (m_decodeW == size.width() && m_decodeH == size.height())
        return;

    // A new decode size invalidates the cached images (they are the wrong size)
    // but NOT the demux position — there is no reason to seek.
    m_decodeW = size.width();
    m_decodeH = size.height();
    m_videoCache.clear();
    m_nv12Cache.clear();
}

drift::TimeUs ClipReader::frameToleranceUs() const
{
    // Half a source frame: the nearest-frame window. The old fixed 40 ms was
    // longer than a frame above ~25 fps, so it returned stale frames.
    if (m_sourceFrameDurationUs > 0)
        return qMax<drift::TimeUs>(1, m_sourceFrameDurationUs / 2);
    return 20'000;
}

bool ClipReader::lookupCachedFrame(drift::TimeUs sourceUs, QImage &out) const
{
    const drift::TimeUs tolerance = frameToleranceUs();
    drift::TimeUs bestDelta = tolerance + 1;
    int bestIndex = -1;
    for (int i = 0; i < m_videoCache.size(); ++i) {
        const drift::TimeUs delta = qAbs(m_videoCache.at(i).ptsUs - sourceUs);
        if (delta <= tolerance && delta < bestDelta) {
            bestDelta = delta;
            bestIndex = i;
        }
    }
    if (bestIndex < 0)
        return false;

    out = m_videoCache.at(bestIndex).image;
    return true;
}

void ClipReader::storeCachedFrame(drift::TimeUs ptsUs, const QImage &image)
{
    if (image.isNull())
        return;

    for (int i = 0; i < m_videoCache.size(); ++i) {
        if (m_videoCache.at(i).ptsUs == ptsUs) {
            m_videoCache.move(i, 0);
            return;
        }
    }

    m_videoCache.prepend(CachedFrame{ptsUs, image});
    while (m_videoCache.size() > kMaxCachedFrames)
        m_videoCache.removeLast();
}

bool ClipReader::lookupCachedNv12(drift::TimeUs sourceUs, Nv12Frame &out) const
{
    const drift::TimeUs tolerance = frameToleranceUs();
    drift::TimeUs bestDelta = tolerance + 1;
    int bestIndex = -1;
    for (int i = 0; i < m_nv12Cache.size(); ++i) {
        const drift::TimeUs delta = qAbs(m_nv12Cache.at(i).ptsUs - sourceUs);
        if (delta <= tolerance && delta < bestDelta) {
            bestDelta = delta;
            bestIndex = i;
        }
    }
    if (bestIndex < 0)
        return false;

    out = m_nv12Cache.at(bestIndex).frame;
    return out.isValid();
}

void ClipReader::storeCachedNv12(drift::TimeUs ptsUs, const Nv12Frame &frame)
{
    if (!frame.isValid())
        return;

    for (int i = 0; i < m_nv12Cache.size(); ++i) {
        if (m_nv12Cache.at(i).ptsUs == ptsUs) {
            m_nv12Cache.move(i, 0);
            return;
        }
    }

    m_nv12Cache.prepend(CachedNv12{ptsUs, frame});
    trimNv12Cache();
}

int ClipReader::nv12CacheCapacity() const
{
    if (m_readAheadUs <= 0 || m_sourceFrameDurationUs <= 0)
        return kMaxCachedFrames;

    const int aheadFrames =
        qBound(0, static_cast<int>(m_readAheadUs / m_sourceFrameDurationUs), kMaxReadAheadFrames);
    // The history slots stay reserved on top of the read-ahead: time_echo and
    // backward scrubbing read behind the playhead and must not lose their frames
    // to the buffer in front of it.
    int capacity = kMaxCachedFrames + aheadFrames;

    const qsizetype frameBytes = m_nv12Cache.isEmpty() ? 0 : m_nv12Cache.constFirst().frame.data.size();
    if (frameBytes > 0)
        capacity = qMin<qsizetype>(capacity, qMax<qsizetype>(kMaxCachedFrames,
                                                            kNv12CacheByteBudget / frameBytes));
    return capacity;
}

void ClipReader::trimNv12Cache()
{
    const int capacity = nv12CacheCapacity();
    while (m_nv12Cache.size() > capacity) {
        // Evict what playback is furthest past, not what was decoded longest ago:
        // plain insertion order would drop the read-ahead frames first, which are
        // precisely the ones about to be shown. Frames behind the last requested
        // position go before any frame in front of it.
        int worst = 0;
        drift::TimeUs worstRank = std::numeric_limits<drift::TimeUs>::min();
        for (int i = 0; i < m_nv12Cache.size(); ++i) {
            const drift::TimeUs delta = m_lastRequestedNv12Us - m_nv12Cache.at(i).ptsUs;
            const drift::TimeUs rank = delta >= 0 ? delta + std::numeric_limits<qint32>::max() : -delta;
            if (rank > worstRank) {
                worstRank = rank;
                worst = i;
            }
        }
        m_nv12Cache.removeAt(worst);
    }
}

bool ClipReader::wantsMoreNv12ReadAhead() const
{
    if (m_readAheadUs <= 0 || !m_videoPositioned || m_sourceFrameDurationUs <= 0)
        return false;
    if (m_nv12Cache.size() >= nv12CacheCapacity())
        return false;
    return m_lastVideoPtsUs - m_lastRequestedNv12Us < m_readAheadUs;
}

void ClipReader::close()
{
    if (m_swr)
        swr_free(&m_swr);

    teardownVideoDecoder();

    if (m_audioCtx)
        avcodec_free_context(&m_audioCtx);
    if (m_fmt)
        avformat_close_input(&m_fmt);

    m_videoStream = -1;
    m_audioStream = -1;
    m_hwAccelDisabled = false;
    m_hwScalerFailed = false;
    m_audioPositioned = false;
    m_audioNextPtsUs = 0;
    m_audioLeftover.clear();
    m_path.clear();
}

bool ClipReader::open(const QString &path)
{
    if (path.isEmpty())
        return false;
    if (m_path == path && isOpen())
        return true;

    close();
    m_path = path;

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    m_fmt = fmt;
    for (unsigned i = 0; i < m_fmt->nb_streams; ++i) {
        const AVMediaType type = m_fmt->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && m_videoStream < 0)
            m_videoStream = static_cast<int>(i);
        else if (type == AVMEDIA_TYPE_AUDIO && m_audioStream < 0)
            m_audioStream = static_cast<int>(i);
    }

    if (m_videoStream >= 0) {
        const AVRational rate = m_fmt->streams[m_videoStream]->avg_frame_rate;
        if (rate.num > 0 && rate.den > 0) {
            m_sourceFrameDurationUs =
                static_cast<drift::TimeUs>(std::llround(drift::kUsPerSecond * double(rate.den) / rate.num));
        }
    }

    return hasVideo() || hasAudio();
}

bool ClipReader::openSoftwareVideoDecoder()
{
    if (!m_fmt || m_videoStream < 0)
        return false;

    const AVCodecParameters *par = m_fmt->streams[m_videoStream]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec)
        return false;

    m_videoCtx = avcodec_alloc_context3(codec);
    if (!m_videoCtx)
        return false;

    if (avcodec_parameters_to_context(m_videoCtx, par) < 0) {
        avcodec_free_context(&m_videoCtx);
        return false;
    }

    // Left at defaults this decodes single-threaded on most builds. Each reader
    // already owns a thread, so keep the fan-out modest rather than per-core.
    m_videoCtx->thread_count = qBound(1, QThread::idealThreadCount() / 2, 4);
    m_videoCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    if (avcodec_open2(m_videoCtx, codec, nullptr) < 0) {
        avcodec_free_context(&m_videoCtx);
        return false;
    }

    m_hwAccelActive = false;
    m_hwPixFmt = AV_PIX_FMT_NONE;
    return true;
}

// VAAPI decode is ~8x faster than software, but the GPU->CPU readback that has to
// follow costs ~1 ms/frame even after a VPP downscale, and no readback is needed at
// all in software. Cheap streams decode for far less than that, so hwaccel makes
// them slower — a 1008 kbit/s Constrained Baseline screen recording decodes in
// 0.02 ms/frame in software but takes 2.4 ms/frame just to read back.
// Measured on iHD, 1080p: 63 kbit/frame -> 0.02 ms/frame software,
// 490 kbit/frame -> 1.30 ms/frame. The crossover sits well between the two.
constexpr double kHwAccelMinKbitPerFrame = 250.0;

bool ClipReader::hardwareDecodeIsWorthIt() const
{
    const AVStream *stream = m_fmt->streams[m_videoStream];
    const AVCodecParameters *par = stream->codecpar;

    // 4K and up is expensive in software at any bitrate, and with the VPP downscale
    // the readback is bounded by the preview size rather than the source size.
    if (int64_t(par->width) * par->height >= 3840LL * 2160)
        return true;

    int64_t bitRate = par->bit_rate;
    if (bitRate <= 0)
        bitRate = m_fmt->bit_rate; // Matroska usually omits the per-stream value
    if (bitRate <= 0)
        return true;

    const AVRational rate = stream->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0)
        return true;

    const double fps = double(rate.num) / double(rate.den);
    return (double(bitRate) / fps / 1000.0) >= kHwAccelMinKbitPerFrame;
}

bool ClipReader::tryOpenHardwareDecoder()
{
    if (!m_fmt || m_videoStream < 0 || m_hwAccelActive || m_hwAccelDisabled)
        return m_hwAccelActive;

#ifdef Q_OS_ANDROID
    // The only hardware path below is VAAPI, which is Linux-only, and the Android FFmpeg is
    // configured --disable-vaapi. MediaCodec is the eventual replacement and belongs right here,
    // but it is not a drop-in: AV_PIX_FMT_MEDIACODEC is an opaque SurfaceTexture handle, and the
    // compositor seeks randomly per frame, which is the access pattern MediaCodec's pipeline and
    // flush cost handle worst. That needs measuring on its own rather than on the way to first
    // pixels, so software decode it is — the sticky flag below is exactly the existing mechanism
    // for saying so, and everything downstream already handles it.
    m_hwAccelDisabled = true;
    return false;
#else
    // Allow forcing software decode on broken VAAPI stacks.
    if (qEnvironmentVariableIsSet("DRIFT_NO_VAAPI")) {
        m_hwAccelDisabled = true;
        return false;
    }

    if (!qEnvironmentVariableIsSet("DRIFT_FORCE_VAAPI") && !hardwareDecodeIsWorthIt()) {
        m_hwAccelDisabled = true;
        return false;
    }

    const AVCodecParameters *par = m_fmt->streams[m_videoStream]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec)
        return false;

    for (int i = 0;; ++i) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config)
            break;
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
            && config->device_type == AV_HWDEVICE_TYPE_VAAPI) {
            m_hwPixFmt = config->pix_fmt;
            break;
        }
    }

    if (m_hwPixFmt == AV_PIX_FMT_NONE)
        return false;

    if (av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0) < 0) {
        m_hwPixFmt = AV_PIX_FMT_NONE;
        if (m_hwDeviceCtx)
            av_buffer_unref(&m_hwDeviceCtx);
        m_hwAccelDisabled = true;
        return false;
    }

    m_videoCtx = avcodec_alloc_context3(codec);
    if (!m_videoCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwPixFmt = AV_PIX_FMT_NONE;
        return false;
    }

    if (avcodec_parameters_to_context(m_videoCtx, par) < 0) {
        avcodec_free_context(&m_videoCtx);
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwPixFmt = AV_PIX_FMT_NONE;
        return false;
    }

    m_videoCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
    m_videoCtx->opaque = &m_hwPixFmt;
    m_videoCtx->get_format = hwGetFormat;

    if (avcodec_open2(m_videoCtx, codec, nullptr) < 0) {
        avcodec_free_context(&m_videoCtx);
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwPixFmt = AV_PIX_FMT_NONE;
        m_hwAccelDisabled = true;
        return false;
    }

    m_hwAccelActive = true;
    return true;
#endif // Q_OS_ANDROID
}

bool ClipReader::fallbackFromHardwareDecoder()
{
    if (!m_hwAccelActive && !m_hwDeviceCtx)
        return openSoftwareVideoDecoder();

    teardownVideoDecoder();
    m_hwAccelDisabled = true;
    return openSoftwareVideoDecoder();
}

bool ClipReader::ensureVideoDecoder()
{
    if (!m_fmt || m_videoStream < 0)
        return false;
    if (m_videoCtx)
        return true;

    if (tryOpenHardwareDecoder())
        return true;

    return openSoftwareVideoDecoder();
}

void ClipReader::teardownHwScaler()
{
    if (m_vppGraph)
        avfilter_graph_free(&m_vppGraph);
    m_vppSrc = nullptr;
    m_vppSink = nullptr;
    if (m_vppFramesCtx)
        av_buffer_unref(&m_vppFramesCtx);
    av_frame_free(&m_vppScaled);
    av_frame_free(&m_swFrame);
    m_vppW = 0;
    m_vppH = 0;
}

bool ClipReader::ensureHwScaler(const AVFrame *hwFrame, int targetWidth, int targetHeight)
{
    if (m_hwScalerFailed || !hwFrame->hw_frames_ctx)
        return false;

    // Rebuild when the caller's decode size changes, or when the decoder handed us
    // a new frame pool (it reallocates on resolution changes and after a flush).
    if (m_vppGraph && m_vppW == targetWidth && m_vppH == targetHeight && m_vppFramesCtx
        && m_vppFramesCtx->data == hwFrame->hw_frames_ctx->data) {
        return true;
    }

    teardownHwScaler();

    m_vppGraph = avfilter_graph_alloc();
    m_vppScaled = av_frame_alloc();
    m_swFrame = av_frame_alloc();
    if (!m_vppGraph || !m_vppScaled || !m_swFrame) {
        teardownHwScaler();
        m_hwScalerFailed = true;
        return false;
    }

    const AVFilter *bufferFilter = avfilter_get_by_name("buffer");
    const AVFilter *sinkFilter = avfilter_get_by_name("buffersink");
    const AVFilter *scaleFilter = avfilter_get_by_name("scale_vaapi");
    if (!bufferFilter || !sinkFilter || !scaleFilter) {
        teardownHwScaler();
        m_hwScalerFailed = true;
        return false;
    }

    // The source has to know the hw frame pool before it is initialized —
    // "buffer" rejects a hardware pix_fmt with a null hw_frames_ctx.
    m_vppSrc = avfilter_graph_alloc_filter(m_vppGraph, bufferFilter, "in");
    if (!m_vppSrc) {
        teardownHwScaler();
        m_hwScalerFailed = true;
        return false;
    }

    AVBufferSrcParameters *params = av_buffersrc_parameters_alloc();
    if (!params) {
        teardownHwScaler();
        m_hwScalerFailed = true;
        return false;
    }
    params->format = hwFrame->format;
    params->width = hwFrame->width;
    params->height = hwFrame->height;
    params->time_base = m_fmt->streams[m_videoStream]->time_base;
    params->hw_frames_ctx = hwFrame->hw_frames_ctx;
    const int paramsRc = av_buffersrc_parameters_set(m_vppSrc, params);
    av_free(params);
    if (paramsRc < 0 || avfilter_init_str(m_vppSrc, nullptr) < 0) {
        teardownHwScaler();
        m_hwScalerFailed = true;
        return false;
    }

    AVFilterContext *scale = nullptr;
    const QByteArray scaleArgs =
        QByteArray("w=") + QByteArray::number(targetWidth) + ":h=" + QByteArray::number(targetHeight);
    if (avfilter_graph_create_filter(&scale, scaleFilter, "vpp", scaleArgs.constData(), nullptr,
                                     m_vppGraph)
            < 0
        || avfilter_graph_create_filter(&m_vppSink, sinkFilter, "out", nullptr, nullptr, m_vppGraph) < 0
        || avfilter_link(m_vppSrc, 0, scale, 0) < 0 || avfilter_link(scale, 0, m_vppSink, 0) < 0
        || avfilter_graph_config(m_vppGraph, nullptr) < 0) {
        teardownHwScaler();
        m_hwScalerFailed = true;
        return false;
    }

    m_vppFramesCtx = av_buffer_ref(hwFrame->hw_frames_ctx);
    m_vppW = targetWidth;
    m_vppH = targetHeight;
    return true;
}

AVFrame *ClipReader::hwFrameToSoftware(const AVFrame *hwFrame, int targetWidth, int targetHeight)
{
    // Downscale on the GPU first when we can: the readback is the dominant cost of
    // the whole hwaccel path and it is proportional to the surface area, so moving
    // preview-sized pixels instead of full-resolution ones is most of the win.
    if (ensureHwScaler(hwFrame, targetWidth, targetHeight)) {
        av_frame_unref(m_vppScaled);
        av_frame_unref(m_swFrame);
        if (av_buffersrc_add_frame_flags(m_vppSrc, const_cast<AVFrame *>(hwFrame),
                                         AV_BUFFERSRC_FLAG_KEEP_REF)
                >= 0
            && av_buffersink_get_frame(m_vppSink, m_vppScaled) >= 0) {
            const int rc = av_hwframe_transfer_data(m_swFrame, m_vppScaled, 0);
            av_frame_unref(m_vppScaled);
            if (rc >= 0)
                return m_swFrame;
            av_frame_unref(m_swFrame);
        }
        // VPP is configured but misbehaving — stop using it and transfer full size.
        m_hwScalerFailed = true;
        teardownHwScaler();
    }

    if (!m_swFrame) {
        m_swFrame = av_frame_alloc();
        if (!m_swFrame)
            return nullptr;
    }
    av_frame_unref(m_swFrame);
    if (av_hwframe_transfer_data(m_swFrame, hwFrame, 0) < 0) {
        av_frame_unref(m_swFrame);
        return nullptr;
    }
    return m_swFrame;
}

bool ClipReader::transferHwFrameToImage(const AVFrame *hwFrame, QImage &out, int targetWidth, int targetHeight)
{
    const AVFrame *swFrame = hwFrameToSoftware(hwFrame, targetWidth, targetHeight);
    if (!swFrame)
        return false;

    const QImage image = frameToRgba(swFrame, m_sws, targetWidth, targetHeight);
    if (image.isNull())
        return false;

    out = image;
    return true;
}

bool ClipReader::convertFrame(const AVFrame *frame, QImage &out, int targetWidth, int targetHeight)
{
    if (!frame)
        return false;

    if (m_hwAccelActive && frame->format == m_hwPixFmt)
        return transferHwFrameToImage(frame, out, targetWidth, targetHeight);

    // If get_format fell back to software while hw_device_ctx is still set,
    // treat the frame as a normal software frame.
    if (isHardwarePixelFormat(static_cast<AVPixelFormat>(frame->format)))
        return transferHwFrameToImage(frame, out, targetWidth, targetHeight);

    const QImage image = frameToRgba(frame, m_sws, targetWidth, targetHeight);
    if (image.isNull())
        return false;
    out = image;
    return true;
}

bool ClipReader::convertFrameNv12(const AVFrame *frame, Nv12Frame &out, int targetWidth, int targetHeight)
{
    if (!frame)
        return false;

    const AVFrame *swFrame = frame;
    if ((m_hwAccelActive && frame->format == m_hwPixFmt)
        || isHardwarePixelFormat(static_cast<AVPixelFormat>(frame->format))) {
        swFrame = hwFrameToSoftware(frame, targetWidth, targetHeight);
        if (!swFrame)
            return false;
    }

    out = frameToNv12(swFrame, m_swsNv12, targetWidth, targetHeight);
    return out.isValid();
}

bool ClipReader::ensureAudioDecoder()
{
    if (!m_fmt || m_audioStream < 0)
        return false;
    if (m_audioCtx)
        return true;

    const AVCodecParameters *par = m_fmt->streams[m_audioStream]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec)
        return false;

    m_audioCtx = avcodec_alloc_context3(codec);
    if (!m_audioCtx)
        return false;
    if (avcodec_parameters_to_context(m_audioCtx, par) < 0) {
        avcodec_free_context(&m_audioCtx);
        return false;
    }
    if (avcodec_open2(m_audioCtx, codec, nullptr) < 0) {
        avcodec_free_context(&m_audioCtx);
        return false;
    }
    return true;
}

bool ClipReader::seekVideoStream(drift::TimeUs sourceUs)
{
    if (!ensureVideoDecoder())
        return false;

    AVStream *stream = m_fmt->streams[m_videoStream];
    const int64_t targetTs = av_rescale_q(sourceUs, {1, AV_TIME_BASE}, stream->time_base);
    if (av_seek_frame(m_fmt, m_videoStream, targetTs, AVSEEK_FLAG_BACKWARD) < 0) {
        if (sourceUs > 0)
            return false;
        av_seek_frame(m_fmt, m_videoStream, 0, AVSEEK_FLAG_BACKWARD);
    }
    avcodec_flush_buffers(m_videoCtx);
    m_videoPositioned = true;
    return true;
}

bool ClipReader::seekAudioStream(drift::TimeUs sourceUs)
{
    if (!ensureAudioDecoder())
        return false;

    AVStream *stream = m_fmt->streams[m_audioStream];
    const int64_t targetTs = av_rescale_q(sourceUs, {1, AV_TIME_BASE}, stream->time_base);
    if (av_seek_frame(m_fmt, m_audioStream, targetTs, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(m_audioCtx);
    if (m_swr)
        swr_free(&m_swr);
    return true;
}

bool ClipReader::decodeVideoFrameAtOnce(drift::TimeUs sourceUs, QImage &out, int maxWidth, int maxHeight,
                                        bool *hwFailure)
{
    if (hwFailure)
        *hwFailure = false;
    if (!ensureVideoDecoder())
        return false;

    applyDecodeSize(decodeSizeFor(maxWidth, maxHeight));

    if (lookupCachedFrame(sourceUs, out))
        return true;

    const drift::TimeUs tolerance = frameToleranceUs();
    const bool needSeek = !m_videoPositioned || sourceUs < m_lastVideoPtsUs - tolerance
                          || sourceUs - m_lastVideoPtsUs > kForwardSeekThresholdUs;
    if (needSeek && !seekVideoStream(sourceUs))
        return false;

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *best = av_frame_alloc();
    if (!packet || !frame || !best) {
        av_frame_free(&best);
        av_frame_free(&frame);
        av_packet_free(&packet);
        return false;
    }

    const AVRational timeBase = m_fmt->streams[m_videoStream]->time_base;
    drift::TimeUs bestDelta = INT64_MAX;
    drift::TimeUs bestPtsUs = 0;
    bool found = false;
    bool done = false;
    bool sawHwFailure = false;

    auto markHwFailure = [&]() {
        if (m_hwAccelActive) {
            sawHwFailure = true;
            done = true;
        }
    };

    // Everything the decoder has ready, keeping the frame closest to sourceUs. Shared with the
    // end-of-stream drain below so both select the same way.
    auto receiveFrames = [&] {
        while (!done) {
            const int rc = avcodec_receive_frame(m_videoCtx, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                break;
            if (rc < 0) {
                // VAAPI often fails here with "hardware accelerator failed to
                // decode picture". The frame may be partially initialized —
                // unref before any further use or free.
                av_frame_unref(frame);
                markHwFailure();
                break;
            }

            const drift::TimeUs ptsUs = ptsToUs(frame, timeBase);
            m_lastVideoPtsUs = ptsUs;
            const drift::TimeUs delta = qAbs(ptsUs - sourceUs);
            // Keep a reference to the best frame and convert only once, after the
            // loop. Converting every frame between the keyframe and the target was
            // an sws_scale + full copy per frame of the GOP, all but one discarded.
            if (delta < bestDelta) {
                bestDelta = delta;
                bestPtsUs = ptsUs;
                av_frame_unref(best);
                if (av_frame_ref(best, frame) < 0) {
                    av_frame_unref(frame);
                    done = true;
                    break;
                }
                found = true;
            }
            av_frame_unref(frame);

            if (ptsUs >= sourceUs) {
                done = true;
                break;
            }
        }
    };

    bool eof = false;
    while (!done) {
        if (av_read_frame(m_fmt, packet) < 0) {
            eof = true;
            break;
        }
        if (packet->stream_index != m_videoStream) {
            av_packet_unref(packet);
            continue;
        }

        int sendRc = avcodec_send_packet(m_videoCtx, packet);
        av_packet_unref(packet);
        if (sendRc == AVERROR(EAGAIN)) {
            // Decoder is full; drain below then retry is handled by the next read.
            // Fall through to receive.
        } else if (sendRc < 0) {
            markHwFailure();
            continue;
        }

        receiveFrames();
    }

    // A frame-threaded decoder still holds several frames after the last packet is sent, so
    // running out of packets is not the same as running out of frames. Without this drain the
    // tail of every clip is undecodable — the loop above just ends and those frames are never
    // received, which is exactly what a seek near the end of a clip asks for. The audio path
    // has always drained here; the video path did not.
    bool drained = false;
    if (eof && !done && !sawHwFailure) {
        avcodec_send_packet(m_videoCtx, nullptr);
        receiveFrames();
        // Leaves the decoder usable; the demuxer is at EOF, so the next call has to seek.
        avcodec_flush_buffers(m_videoCtx);
        drained = true;
    }

    QImage converted;
    bool convertedOk = false;
    if (found && !sawHwFailure) {
        convertedOk = convertFrame(best, converted, m_decodeW, m_decodeH);
        if (!convertedOk && m_hwAccelActive
            && (best->format == m_hwPixFmt
                || isHardwarePixelFormat(static_cast<AVPixelFormat>(best->format)))) {
            // Transfer from the VAAPI surface failed — abandon hwaccel.
            sawHwFailure = true;
        }
    }

    av_frame_unref(best);
    av_frame_free(&best);
    av_frame_unref(frame);
    av_frame_free(&frame);
    av_packet_free(&packet);

    if (sawHwFailure) {
        if (hwFailure)
            *hwFailure = true;
        m_videoPositioned = false;
        return false;
    }

    if (convertedOk) {
        out = converted;
        storeCachedFrame(bestPtsUs, converted);
        m_videoPositioned = !drained;
        return true;
    }

    m_videoPositioned = false;
    return false;
}

bool ClipReader::readVideoFrameAt(drift::TimeUs sourceUs, QImage &out, int maxWidth, int maxHeight)
{
    bool hwFailure = false;
    if (decodeVideoFrameAtOnce(sourceUs, out, maxWidth, maxHeight, &hwFailure))
        return true;

    if (!hwFailure)
        return false;

    // Sticky software fallback for this reader — continuing with a broken VAAPI
    // context is what triggers free(): invalid size on subsequent frames.
    if (!fallbackFromHardwareDecoder())
        return false;

    return decodeVideoFrameAtOnce(sourceUs, out, maxWidth, maxHeight, nullptr);
}

bool ClipReader::decodeVideoFrameAtOnceNv12(drift::TimeUs sourceUs, Nv12Frame &out, int maxWidth,
                                            int maxHeight, bool *hwFailure)
{
    if (hwFailure)
        *hwFailure = false;
    if (!ensureVideoDecoder())
        return false;

    applyDecodeSize(decodeSizeFor(maxWidth, maxHeight));

    if (lookupCachedNv12(sourceUs, out))
        return true;

    const drift::TimeUs tolerance = frameToleranceUs();
    const bool needSeek = !m_videoPositioned || sourceUs < m_lastVideoPtsUs - tolerance
                          || sourceUs - m_lastVideoPtsUs > kForwardSeekThresholdUs;
    if (needSeek && !seekVideoStream(sourceUs))
        return false;

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *best = av_frame_alloc();
    if (!packet || !frame || !best) {
        av_frame_free(&best);
        av_frame_free(&frame);
        av_packet_free(&packet);
        return false;
    }

    const AVRational timeBase = m_fmt->streams[m_videoStream]->time_base;
    drift::TimeUs bestDelta = INT64_MAX;
    drift::TimeUs bestPtsUs = 0;
    bool found = false;
    bool done = false;
    bool sawHwFailure = false;

    auto markHwFailure = [&]() {
        if (m_hwAccelActive) {
            sawHwFailure = true;
            done = true;
        }
    };

    // Same selection for the read loop and the end-of-stream drain below.
    auto receiveFrames = [&] {
        while (!done) {
            const int rc = avcodec_receive_frame(m_videoCtx, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                break;
            if (rc < 0) {
                av_frame_unref(frame);
                markHwFailure();
                break;
            }

            const drift::TimeUs ptsUs = ptsToUs(frame, timeBase);
            m_lastVideoPtsUs = ptsUs;
            const drift::TimeUs delta = qAbs(ptsUs - sourceUs);
            if (delta < bestDelta) {
                bestDelta = delta;
                bestPtsUs = ptsUs;
                av_frame_unref(best);
                if (av_frame_ref(best, frame) < 0) {
                    av_frame_unref(frame);
                    done = true;
                    break;
                }
                found = true;
            }
            av_frame_unref(frame);

            if (ptsUs >= sourceUs) {
                done = true;
                break;
            }
        }
    };

    bool eof = false;
    while (!done) {
        if (av_read_frame(m_fmt, packet) < 0) {
            eof = true;
            break;
        }
        if (packet->stream_index != m_videoStream) {
            av_packet_unref(packet);
            continue;
        }

        int sendRc = avcodec_send_packet(m_videoCtx, packet);
        av_packet_unref(packet);
        if (sendRc == AVERROR(EAGAIN)) {
            // Fall through to receive.
        } else if (sendRc < 0) {
            markHwFailure();
            continue;
        }

        receiveFrames();
    }

    // See decodeVideoFrameAtOnce: out of packets is not out of frames.
    bool drained = false;
    if (eof && !done && !sawHwFailure) {
        avcodec_send_packet(m_videoCtx, nullptr);
        receiveFrames();
        avcodec_flush_buffers(m_videoCtx);
        drained = true;
    }

    Nv12Frame converted;
    bool convertedOk = false;
    if (found && !sawHwFailure) {
        convertedOk = convertFrameNv12(best, converted, m_decodeW, m_decodeH);
        if (!convertedOk && m_hwAccelActive
            && (best->format == m_hwPixFmt
                || isHardwarePixelFormat(static_cast<AVPixelFormat>(best->format)))) {
            sawHwFailure = true;
        }
    }

    av_frame_unref(best);
    av_frame_free(&best);
    av_frame_unref(frame);
    av_frame_free(&frame);
    av_packet_free(&packet);

    if (sawHwFailure) {
        if (hwFailure)
            *hwFailure = true;
        m_videoPositioned = false;
        return false;
    }

    if (convertedOk) {
        out = converted;
        storeCachedNv12(bestPtsUs, converted);
        m_videoPositioned = !drained;
        return true;
    }

    m_videoPositioned = false;
    return false;
}

bool ClipReader::readVideoFrameAtNv12(drift::TimeUs sourceUs, Nv12Frame &out, int maxWidth, int maxHeight)
{
    if (!m_prefetching)
        m_lastRequestedNv12Us = sourceUs;

    bool hwFailure = false;
    if (decodeVideoFrameAtOnceNv12(sourceUs, out, maxWidth, maxHeight, &hwFailure))
        return true;

    if (!hwFailure)
        return false;

    if (!fallbackFromHardwareDecoder())
        return false;

    return decodeVideoFrameAtOnceNv12(sourceUs, out, maxWidth, maxHeight, nullptr);
}

void ClipReader::prefetchNextVideoFrame(int maxWidth, int maxHeight)
{
    if (!m_videoPositioned || m_sourceFrameDurationUs <= 0)
        return;

    QImage ignored;
    readVideoFrameAt(m_lastVideoPtsUs + m_sourceFrameDurationUs, ignored, maxWidth, maxHeight);
}

bool ClipReader::prefetchNextVideoFrameNv12(int maxWidth, int maxHeight, drift::TimeUs readAheadUs)
{
    // Sticky: the caller passes the current depth on every prefetch, so dropping
    // to 0 (playback stopped) shrinks the cache back on the next call.
    m_readAheadUs = qMax<drift::TimeUs>(0, readAheadUs);
    trimNv12Cache();

    if (!m_videoPositioned || m_sourceFrameDurationUs <= 0)
        return false;

    // Step over frames the buffer already holds. A cache hit leaves the decoder
    // where it is, so walking by decoder position alone would ask for the same
    // frame forever once the walk reaches an earlier run's frames — which is
    // exactly what a backward seek into a buffered region sets up.
    drift::TimeUs target = m_lastVideoPtsUs + m_sourceFrameDurationUs;
    Nv12Frame cached;
    while (target - m_lastRequestedNv12Us < m_readAheadUs && lookupCachedNv12(target, cached))
        target += m_sourceFrameDurationUs;

    if (m_readAheadUs > 0 && target - m_lastRequestedNv12Us >= m_readAheadUs)
        return false;

    Nv12Frame ignored;
    m_prefetching = true;
    const bool decoded = readVideoFrameAtNv12(target, ignored, maxWidth, maxHeight);
    m_prefetching = false;

    return decoded && wantsMoreNv12ReadAhead();
}

int ClipReader::readAudioInterleaved(drift::TimeUs sourceStartUs, int sampleCount, int outputSampleRate,
                                     float *interleavedStereoOut)
{
    if (!interleavedStereoOut || sampleCount <= 0 || outputSampleRate <= 0)
        return 0;
    if (!ensureAudioDecoder())
        return 0;

    // Re-seek only on a real discontinuity. During normal playback the request
    // advances by exactly one buffer, so we keep decoding forward from where we
    // left off — no per-buffer seek, no resampler reset, no glitching.
    const bool rateChanged = m_outputSampleRate != outputSampleRate;
    m_outputSampleRate = outputSampleRate;
    const bool needSeek = rateChanged || !m_audioPositioned
                          || sourceStartUs < m_audioNextPtsUs - kAudioSeekToleranceUs
                          || sourceStartUs > m_audioNextPtsUs + kAudioForwardSeekThresholdUs;

    bool alignToStart = false;
    if (needSeek) {
        if (!seekAudioStream(sourceStartUs)) // flushes the codec and frees m_swr
            return 0;
        m_audioLeftover.clear();
        m_audioPositioned = true;
        alignToStart = true;
    }

    if (!m_swr) {
        AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&m_swr, &outLayout, AV_SAMPLE_FMT_FLT, outputSampleRate,
                                &m_audioCtx->ch_layout, static_cast<AVSampleFormat>(m_audioCtx->sample_fmt),
                                m_audioCtx->sample_rate, 0, nullptr)
                < 0
            || swr_init(m_swr) < 0) {
            if (m_swr)
                swr_free(&m_swr);
            return 0;
        }
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!packet || !frame) {
        av_frame_free(&frame);
        av_packet_free(&packet);
        return 0;
    }

    const AVRational timeBase = m_fmt->streams[m_audioStream]->time_base;
    QVector<float> scratch;
    int pendingDrop = 0; // leading output frames to discard so playback starts at sourceStartUs
    bool sentFlush = false;

    while (m_audioLeftover.size() < sampleCount * 2) {
        const int rc = avcodec_receive_frame(m_audioCtx, frame);
        if (rc == AVERROR(EAGAIN)) {
            if (sentFlush)
                break;
            if (av_read_frame(m_fmt, packet) < 0) {
                avcodec_send_packet(m_audioCtx, nullptr); // drain the decoder at EOF
                sentFlush = true;
                continue;
            }
            if (packet->stream_index != m_audioStream) {
                av_packet_unref(packet);
                continue;
            }
            avcodec_send_packet(m_audioCtx, packet);
            av_packet_unref(packet);
            continue;
        }
        if (rc < 0) { // AVERROR_EOF or a decode error
            av_frame_unref(frame);
            break;
        }

        if (alignToStart) {
            const drift::TimeUs framePtsUs = ptsToUs(frame, timeBase);
            m_audioNextPtsUs = framePtsUs;
            if (sourceStartUs > framePtsUs)
                pendingDrop = static_cast<int>(((sourceStartUs - framePtsUs) * outputSampleRate)
                                               / drift::kUsPerSecond);
            alignToStart = false;
        }

        const int maxOut = swr_get_out_samples(m_swr, frame->nb_samples);
        scratch.resize(maxOut * 2);
        uint8_t *outData[1] = {reinterpret_cast<uint8_t *>(scratch.data())};
        const int converted = swr_convert(m_swr, outData, maxOut,
                                          const_cast<const uint8_t **>(frame->data), frame->nb_samples);
        if (converted <= 0)
            continue;

        int offset = 0;
        if (pendingDrop > 0) {
            const int drop = qMin(pendingDrop, converted);
            offset = drop;
            pendingDrop -= drop;
            m_audioNextPtsUs += static_cast<drift::TimeUs>(drop) * drift::kUsPerSecond / outputSampleRate;
        }
        for (int i = offset * 2; i < converted * 2; ++i)
            m_audioLeftover.append(scratch[i]);
    }

    av_frame_unref(frame);
    av_frame_free(&frame);
    av_packet_free(&packet);

    const int outFrames = qMin(sampleCount, static_cast<int>(m_audioLeftover.size() / 2));
    if (outFrames > 0) {
        std::memcpy(interleavedStereoOut, m_audioLeftover.constData(),
                    static_cast<size_t>(outFrames) * 2 * sizeof(float));
        m_audioLeftover.remove(0, outFrames * 2);
        m_audioNextPtsUs += static_cast<drift::TimeUs>(outFrames) * drift::kUsPerSecond / outputSampleRate;
    }
    return outFrames;
}

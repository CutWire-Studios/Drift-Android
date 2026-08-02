#include "MediaThumbnail.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QSize>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {

QString cacheDir()
{
    // Memoized: tile lookups hit this per visible tile, and mkpath is a syscall each time.
    static const QString dir = [] {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             + QStringLiteral("/thumbnails");
        QDir().mkpath(path);
        return path;
    }();
    return dir;
}

// Bumped when the thumbnail geometry changes, so stale squashed caches are ignored.
constexpr int kThumbnailCacheVersion = 2;
constexpr int kThumbnailMaxEdge = 320;

QString cacheKeyFor(const QString &sourcePath)
{
    return QString::number(qHash(QFileInfo(sourcePath).absoluteFilePath()))
           + QStringLiteral("_v") + QString::number(kThumbnailCacheVersion);
}

// Thumbnails keep the source display aspect (pixel aspect included) so the media
// library can letterbox them instead of stretching portrait clips into a 16:9 box.
QSize thumbnailSizeFor(int codedWidth, int codedHeight, AVRational sampleAspect)
{
    if (codedWidth <= 0 || codedHeight <= 0)
        return {kThumbnailMaxEdge, kThumbnailMaxEdge * 9 / 16};

    double displayWidth = codedWidth;
    if (sampleAspect.num > 0 && sampleAspect.den > 0)
        displayWidth *= static_cast<double>(sampleAspect.num) / sampleAspect.den;

    const double scale = std::min({1.0,
                                   kThumbnailMaxEdge / displayWidth,
                                   kThumbnailMaxEdge / static_cast<double>(codedHeight)});
    return {std::max(2, static_cast<int>(std::lround(displayWidth * scale))),
            std::max(2, static_cast<int>(std::lround(codedHeight * scale)))};
}

QString cachePathFor(const QString &sourcePath)
{
    return cacheDir() + QLatin1Char('/') + cacheKeyFor(sourcePath) + QStringLiteral(".jpg");
}

QString cacheStripPathFor(const QString &sourcePath)
{
    return cacheDir() + QLatin1Char('/') + cacheKeyFor(sourcePath) + QStringLiteral("_strip.jpg");
}

bool isValidCacheFile(const QString &path)
{
    return QFile::exists(path) && QFileInfo(path).size() > 128;
}

QString tileGlob()
{
    return QStringLiteral("*_v%1_t*.jpg").arg(kThumbnailCacheVersion);
}

double tileSeconds(int level, qint64 index)
{
    return static_cast<double>(index) * std::pow(2.0, level);
}

QImage frameToImage(const AVFrame *frame, int width, int height)
{
    SwsContext *sws = sws_getContext(frame->width, frame->height,
                                     static_cast<AVPixelFormat>(frame->format),
                                     width, height, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                                     nullptr, nullptr, nullptr);
    if (!sws)
        return {};

    AVFrame *rgb = av_frame_alloc();
    if (!rgb) {
        sws_freeContext(sws);
        return {};
    }

    rgb->format = AV_PIX_FMT_RGB24;
    rgb->width = width;
    rgb->height = height;
    if (av_frame_get_buffer(rgb, 0) < 0) {
        av_frame_free(&rgb);
        sws_freeContext(sws);
        return {};
    }

    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, rgb->data, rgb->linesize);

    QImage image(rgb->data[0], width, height, rgb->linesize[0], QImage::Format_RGB888);
    const QImage copy = image.copy();

    av_frame_free(&rgb);
    sws_freeContext(sws);
    return copy;
}

bool decodeNextVideoFrame(AVFormatContext *fmt, int videoStreamIndex, AVCodecContext *codecCtx,
                          AVPacket *packet, AVFrame *frame, QImage &outImage, int width, int height)
{
    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        if (avcodec_send_packet(codecCtx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        while (true) {
            const int rc = avcodec_receive_frame(codecCtx, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                break;
            if (rc < 0)
                return false;

            outImage = frameToImage(frame, width, height);
            return !outImage.isNull();
        }
    }

    return false;
}

bool seekAndDecodeFrame(AVFormatContext *fmt, int videoStreamIndex, AVCodecContext *codecCtx,
                        int64_t timeUs, QImage &outImage, int width, int height)
{
    AVStream *stream = fmt->streams[videoStreamIndex];
    const int64_t targetTs = av_rescale_q(timeUs, {1, AV_TIME_BASE}, stream->time_base);
    av_seek_frame(fmt, videoStreamIndex, targetTs, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecCtx);

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    const bool ok = packet && frame && decodeNextVideoFrame(fmt, videoStreamIndex, codecCtx,
                                                            packet, frame, outImage, width, height);

    av_frame_free(&frame);
    av_packet_free(&packet);
    return ok;
}

bool decodeFirstVideoFrame(AVFormatContext *fmt, int videoStreamIndex, AVCodecContext *codecCtx,
                           const QString &outPath, int width, int height)
{
    avcodec_flush_buffers(codecCtx);

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    QImage image;
    bool saved = false;
    int packetsRead = 0;

    while (!saved && packetsRead < 400 && packet && frame) {
        if (!decodeNextVideoFrame(fmt, videoStreamIndex, codecCtx, packet, frame, image, width, height))
            break;
        ++packetsRead;
        saved = image.save(outPath, "JPG", 85);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    return saved;
}

bool openVideoDecoder(const QString &absolutePath, AVFormatContext **fmtOut,
                      int *videoStreamIndexOut, AVCodecContext **codecCtxOut)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, absolutePath.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    int videoStreamIndex = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = static_cast<int>(i);
            break;
        }
    }

    if (videoStreamIndex < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    const AVCodecParameters *codecPar = fmt->streams[videoStreamIndex]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return false;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, codecPar);
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmt);
        return false;
    }

    *fmtOut = fmt;
    *videoStreamIndexOut = videoStreamIndex;
    *codecCtxOut = codecCtx;
    return true;
}

} // namespace

QString MediaThumbnail::generate(const QString &sourcePath, const QString &kind)
{
    const QString absolutePath = QFileInfo(sourcePath).absoluteFilePath();
    if (absolutePath.isEmpty() || !QFile::exists(absolutePath))
        return {};

    const QString outPath = cachePathFor(absolutePath);
    if (isValidCacheFile(outPath))
        return outPath;

    if (kind == QStringLiteral("image")) {
        QImageReader reader(absolutePath);
        reader.setAutoTransform(true);
        QImage image = reader.read();
        if (image.isNull())
            return {};
        if (image.width() > kThumbnailMaxEdge || image.height() > kThumbnailMaxEdge) {
            image = image.scaled(kThumbnailMaxEdge, kThumbnailMaxEdge,
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (!image.save(outPath, "JPG", 85))
            return {};
        return outPath;
    }

    if (kind != QStringLiteral("video"))
        return {};

    AVFormatContext *fmt = nullptr;
    int videoStreamIndex = -1;
    AVCodecContext *codecCtx = nullptr;
    if (!openVideoDecoder(absolutePath, &fmt, &videoStreamIndex, &codecCtx))
        return {};

    const AVCodecParameters *par = fmt->streams[videoStreamIndex]->codecpar;
    const QSize target = thumbnailSizeFor(par->width, par->height, par->sample_aspect_ratio);
    const bool saved = decodeFirstVideoFrame(fmt, videoStreamIndex, codecCtx, outPath,
                                             target.width(), target.height());

    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmt);

    return saved ? outPath : QString();
}

QString MediaThumbnail::generateFilmstrip(const QString &sourcePath, const QString &kind)
{
    const QString absolutePath = QFileInfo(sourcePath).absoluteFilePath();
    if (absolutePath.isEmpty() || !QFile::exists(absolutePath))
        return {};

    if (kind == QStringLiteral("image"))
        return generate(absolutePath, kind);

    if (kind != QStringLiteral("video"))
        return {};

    const QString outPath = cacheStripPathFor(absolutePath);
    if (isValidCacheFile(outPath))
        return outPath;

    AVFormatContext *fmt = nullptr;
    int videoStreamIndex = -1;
    AVCodecContext *codecCtx = nullptr;
    if (!openVideoDecoder(absolutePath, &fmt, &videoStreamIndex, &codecCtx))
        return {};

    const int64_t durationUs = fmt->duration != AV_NOPTS_VALUE ? fmt->duration : 0;
    const int frameW = kFilmstripFrameWidth;
    const int frameH = kFilmstripFrameHeight;
    const int frameCount = kFilmstripFrameCount;

    QImage strip(frameW * frameCount, frameH, QImage::Format_RGB888);
    strip.fill(Qt::black);

    bool anyFrame = false;
    for (int i = 0; i < frameCount; ++i) {
        const int64_t timeUs = durationUs > 0 ? (durationUs * i) / frameCount : 0;
        QImage frame;
        if (!seekAndDecodeFrame(fmt, videoStreamIndex, codecCtx, timeUs, frame, frameW, frameH))
            continue;

        anyFrame = true;
        QPainter painter(&strip);
        painter.drawImage(i * frameW, 0, frame);
    }

    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmt);

    if (!anyFrame || !strip.save(outPath, "JPG", 85))
        return {};

    return outPath;
}

QString MediaThumbnail::tilePath(const QString &sourcePath, int level, qint64 index)
{
    const QString absolutePath = QFileInfo(sourcePath).absoluteFilePath();
    if (absolutePath.isEmpty())
        return {};

    return cacheDir() + QLatin1Char('/') + cacheKeyFor(absolutePath)
           + QStringLiteral("_t%1_%2.jpg").arg(level).arg(index);
}

QList<qint64> MediaThumbnail::generateTiles(const QString &sourcePath, int level,
                                            const QList<qint64> &indices)
{
    QList<qint64> produced;
    const QString absolutePath = QFileInfo(sourcePath).absoluteFilePath();
    if (absolutePath.isEmpty() || indices.isEmpty() || !QFile::exists(absolutePath))
        return produced;

    QList<qint64> todo;
    for (const qint64 index : indices) {
        if (isValidCacheFile(tilePath(absolutePath, level, index)))
            produced.append(index);
        else
            todo.append(index);
    }
    if (todo.isEmpty())
        return produced;

    std::sort(todo.begin(), todo.end());

    AVFormatContext *fmt = nullptr;
    int videoStreamIndex = -1;
    AVCodecContext *codecCtx = nullptr;
    if (!openVideoDecoder(absolutePath, &fmt, &videoStreamIndex, &codecCtx))
        return produced;

    for (const qint64 index : std::as_const(todo)) {
        const int64_t timeUs = static_cast<int64_t>(tileSeconds(level, index) * 1'000'000.0);
        QImage frame;
        if (!seekAndDecodeFrame(fmt, videoStreamIndex, codecCtx, timeUs, frame,
                                kFilmstripFrameWidth, kFilmstripFrameHeight))
            continue;
        if (frame.save(tilePath(absolutePath, level, index), "JPG", 85))
            produced.append(index);
    }

    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmt);

    return produced;
}

void MediaThumbnail::pruneTileCache(qint64 maxBytes)
{
    QDir dir(cacheDir());
    // Tiles are written once and never rewritten, so modification time is insertion order.
    QFileInfoList tiles = dir.entryInfoList({tileGlob()}, QDir::Files, QDir::Time | QDir::Reversed);

    qint64 total = 0;
    for (const QFileInfo &info : std::as_const(tiles))
        total += info.size();

    for (const QFileInfo &info : std::as_const(tiles)) {
        if (total <= maxBytes)
            break;
        const qint64 size = info.size();
        if (QFile::remove(info.absoluteFilePath()))
            total -= size;
    }
}

QString MediaThumbnail::generateAtTime(const QString &sourcePath, double sourceSeconds)
{
    const QString absolutePath = QFileInfo(sourcePath).absoluteFilePath();
    if (absolutePath.isEmpty() || !QFile::exists(absolutePath))
        return {};

    const QString outPath = cacheDir() + QLatin1Char('/')
                              + cacheKeyFor(absolutePath) + QLatin1Char('_')
                              + QString::number(static_cast<int>(sourceSeconds * 1000))
                              + QStringLiteral(".jpg");
    if (isValidCacheFile(outPath))
        return outPath;

    AVFormatContext *fmt = nullptr;
    int videoStreamIndex = -1;
    AVCodecContext *codecCtx = nullptr;
    if (!openVideoDecoder(absolutePath, &fmt, &videoStreamIndex, &codecCtx))
        return {};

    const int64_t timeUs = static_cast<int64_t>(sourceSeconds * 1'000'000.0);
    QImage frame;
    const bool ok = seekAndDecodeFrame(fmt, videoStreamIndex, codecCtx, timeUs, frame, 320, 180)
                    && frame.save(outPath, "JPG", 85);

    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmt);

    return ok ? outPath : QString();
}

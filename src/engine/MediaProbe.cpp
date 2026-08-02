#include "MediaProbe.h"

#include <cmath>

extern "C" {
#include <libavcodec/codec_desc.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/dict.h>
}

namespace {

int rotationOf(const AVStream *stream)
{
    const AVPacketSideData *sd = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                                          stream->codecpar->nb_coded_side_data,
                                                          AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd)
        return 0;

    double angle = av_display_rotation_get(reinterpret_cast<const int32_t *>(sd->data));
    if (std::isnan(angle))
        angle = 0;

    // Normalize to one of 0/90/180/270, matching how players interpret it.
    int rounded = static_cast<int>(std::lround(-angle));
    rounded %= 360;
    if (rounded < 0)
        rounded += 360;
    return rounded;
}

StreamInfo describeStream(const AVFormatContext *fmt, const AVStream *stream)
{
    StreamInfo info;
    const AVCodecParameters *par = stream->codecpar;

    const AVCodecDescriptor *desc = avcodec_descriptor_get(par->codec_id);
    info.codecName = desc ? QString::fromUtf8(desc->name) : QStringLiteral("unknown");

    if (stream->duration != AV_NOPTS_VALUE) {
        info.durationUs = av_rescale_q(stream->duration, stream->time_base, {1, AV_TIME_BASE});
    } else if (fmt->duration != AV_NOPTS_VALUE) {
        info.durationUs = fmt->duration;
    }

    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        info.type = StreamInfo::Type::Video;
        info.width = par->width;
        info.height = par->height;
        if (stream->avg_frame_rate.den != 0)
            info.fps = av_q2d(stream->avg_frame_rate);
        info.rotationDegrees = rotationOf(stream);
        info.attachedPicture = (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
        break;
    case AVMEDIA_TYPE_AUDIO:
        info.type = StreamInfo::Type::Audio;
        info.sampleRate = par->sample_rate;
        info.channels = par->ch_layout.nb_channels;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        info.type = StreamInfo::Type::Subtitle;
        break;
    default:
        info.type = StreamInfo::Type::Other;
        break;
    }

    return info;
}

} // namespace

MediaInfo MediaProbe::probe(const QString &path)
{
    MediaInfo result;
    result.path = path;

    AVFormatContext *fmt = nullptr;
    const QByteArray pathUtf8 = path.toUtf8();

    int rc = avformat_open_input(&fmt, pathUtf8.constData(), nullptr, nullptr);
    if (rc < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(rc, errBuf, sizeof(errBuf));
        result.errorString = QString::fromUtf8(errBuf);
        return result;
    }

    rc = avformat_find_stream_info(fmt, nullptr);
    if (rc < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(rc, errBuf, sizeof(errBuf));
        result.errorString = QString::fromUtf8(errBuf);
        avformat_close_input(&fmt);
        return result;
    }

    result.durationUs = fmt->duration != AV_NOPTS_VALUE ? fmt->duration : 0;

    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        result.streams.append(describeStream(fmt, fmt->streams[i]));

    result.ok = true;
    avformat_close_input(&fmt);
    return result;
}

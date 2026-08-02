#include "AudioAtempo.h"

#include <cmath>
#include <cstring>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
}

namespace {

QString buildAtempoChain(double tempo)
{
    if (tempo <= 0.0)
        tempo = 1.0;

    QStringList parts;
    double remaining = tempo;
    while (remaining > 2.0 + 1e-6) {
        parts.append(QStringLiteral("atempo=2.0"));
        remaining /= 2.0;
    }
    while (remaining < 0.5 - 1e-6) {
        parts.append(QStringLiteral("atempo=0.5"));
        remaining /= 0.5;
    }
    parts.append(QStringLiteral("atempo=%1").arg(remaining, 0, 'f', 4));
    return parts.join(QLatin1Char(','));
}

QVector<float> runAtempoGraph(const float *interleavedStereo, int sampleCount, int sampleRate, double tempo,
                              int outSampleCount)
{
    QVector<float> result;
    if (!interleavedStereo || sampleCount <= 0 || sampleRate <= 0)
        return result;

    AVFilterGraph *graph = avfilter_graph_alloc();
    if (!graph)
        return result;

    const AVFilter *abuffer = avfilter_get_by_name("abuffer");
    const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffer || !abuffersink) {
        avfilter_graph_free(&graph);
        return result;
    }

    AVFilterContext *srcCtx = nullptr;
    AVFilterContext *sinkCtx = nullptr;

    const QByteArray srcArgs =
        QStringLiteral("time_base=1/%1:sample_rate=%1:sample_fmt=flt:channel_layout=stereo").arg(sampleRate).toUtf8();
    if (avfilter_graph_create_filter(&srcCtx, abuffer, "in", srcArgs.constData(), nullptr, graph) < 0
        || avfilter_graph_create_filter(&sinkCtx, abuffersink, "out", nullptr, nullptr, graph) < 0) {
        avfilter_graph_free(&graph);
        return result;
    }

    const QString filterDesc = QStringLiteral("[in] %1 [out]").arg(buildAtempoChain(tempo));
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");
    outputs->filter_ctx = srcCtx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = sinkCtx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    const QByteArray graphDesc = filterDesc.toUtf8();
    if (avfilter_graph_parse_ptr(graph, graphDesc.constData(), &inputs, &outputs, nullptr) < 0
        || avfilter_graph_config(graph, nullptr) < 0) {
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        avfilter_graph_free(&graph);
        return result;
    }
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        avfilter_graph_free(&graph);
        return result;
    }

    frame->format = AV_SAMPLE_FMT_FLT;
    frame->sample_rate = sampleRate;
    av_channel_layout_default(&frame->ch_layout, 2);
    frame->nb_samples = sampleCount;
    if (av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        avfilter_graph_free(&graph);
        return result;
    }

    for (int i = 0; i < sampleCount; ++i) {
        reinterpret_cast<float *>(frame->data[0])[i * 2] = interleavedStereo[i * 2];
        reinterpret_cast<float *>(frame->data[0])[i * 2 + 1] = interleavedStereo[i * 2 + 1];
    }
    frame->pts = 0;

    if (av_buffersrc_add_frame_flags(srcCtx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        av_frame_free(&frame);
        avfilter_graph_free(&graph);
        return result;
    }
    av_frame_free(&frame);

    result.resize(outSampleCount * 2);
    result.fill(0.0f);
    int written = 0;

    while (written < outSampleCount) {
        AVFrame *outFrame = av_frame_alloc();
        const int ret = av_buffersink_get_frame(sinkCtx, outFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            av_frame_free(&outFrame);
            break;
        }

        const int channels = outFrame->ch_layout.nb_channels;
        const float *data = reinterpret_cast<const float *>(outFrame->data[0]);
        for (int i = 0; i < outFrame->nb_samples && written < outSampleCount; ++i, ++written) {
            result[written * 2] = data[i * channels];
            result[written * 2 + 1] = channels > 1 ? data[i * channels + 1] : data[i * channels];
        }
        av_frame_free(&outFrame);
    }

    avfilter_graph_free(&graph);
    return result;
}

} // namespace

QVector<float> AudioAtempo::apply(const float *interleavedStereo, int sampleCount, int sampleRate, double tempo,
                                  int outSampleCount)
{
    if (qFuzzyCompare(tempo, 1.0)) {
        QVector<float> result(outSampleCount * 2, 0.0f);
        const int frames = qMin(sampleCount, outSampleCount);
        for (int i = 0; i < frames * 2; ++i)
            result[i] = interleavedStereo[i];
        return result;
    }
    return runAtempoGraph(interleavedStereo, sampleCount, sampleRate, tempo, outSampleCount);
}

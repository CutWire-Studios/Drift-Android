#include "TimelineOps.h"

#include <QUuid>

#include <algorithm>

namespace drift {

TimeUs snapTime(const Project &project, TimeUs time, bool snapEnabled, TimeUs playheadUs,
                const QList<TimeUs> &extraTargets)
{
    if (!snapEnabled)
        return qMax<TimeUs>(0, time);

    QList<TimeUs> targets = {0, playheadUs};
    for (const Track &track : project.tracks()) {
        for (const Clip &clip : track.clips) {
            targets.append(clip.timelineStart);
            targets.append(clip.timelineEnd());
        }
    }
    targets.append(extraTargets);

    TimeUs best = time;
    TimeUs bestDistance = kSnapThresholdUs;
    for (TimeUs target : targets) {
        const TimeUs distance = qAbs(target - time);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = target;
        }
    }

    return qMax<TimeUs>(0, best);
}

TimeUs resolveClipStart(const Project &project, const Track &track, int excludeClipIndex,
                        TimeUs desiredStart, TimeUs duration, bool snapEnabled, TimeUs playheadUs,
                        const QList<TimeUs> &extraTargets)
{
    TimeUs start = snapTime(project, desiredStart, snapEnabled, playheadUs, extraTargets);

    struct Interval {
        TimeUs begin;
        TimeUs end;
    };
    QList<Interval> intervals;
    intervals.reserve(track.clips.size());

    for (int i = 0; i < track.clips.size(); ++i) {
        if (i == excludeClipIndex)
            continue;
        const Clip &clip = track.clips.at(i);
        intervals.append({clip.timelineStart, clip.timelineEnd()});
    }

    std::sort(intervals.begin(), intervals.end(),
              [](const Interval &a, const Interval &b) { return a.begin < b.begin; });

    bool adjusted = true;
    while (adjusted) {
        adjusted = false;
        for (const Interval &interval : intervals) {
            if (start < interval.end && start + duration > interval.begin) {
                start = interval.end;
                adjusted = true;
            }
        }
    }

    return qMax<TimeUs>(0, start);
}

TimeUs clampClipStartNoOverlap(const Track &track, const QSet<QString> &excludeIds,
                               TimeUs desiredStart, TimeUs duration)
{
    TimeUs start = qMax<TimeUs>(0, desiredStart);

    struct Interval {
        TimeUs begin;
        TimeUs end;
    };
    QList<Interval> intervals;
    intervals.reserve(track.clips.size());
    for (const Clip &clip : track.clips) {
        if (excludeIds.contains(clip.id))
            continue;
        intervals.append({clip.timelineStart, clip.timelineEnd()});
    }
    std::sort(intervals.begin(), intervals.end(),
              [](const Interval &a, const Interval &b) { return a.begin < b.begin; });

    bool adjusted = true;
    while (adjusted) {
        adjusted = false;
        for (const Interval &interval : intervals) {
            if (start < interval.end && start + duration > interval.begin) {
                start = interval.end;
                adjusted = true;
            }
        }
    }

    return start;
}

TimeUs clampClipStartAgainstLeftNeighbors(const Track &track, const QSet<QString> &excludeIds,
                                          TimeUs currentStart, TimeUs desiredStart)
{
    TimeUs start = qMax<TimeUs>(0, desiredStart);
    TimeUs minStart = 0;
    for (const Clip &clip : track.clips) {
        if (excludeIds.contains(clip.id))
            continue;
        // Only blockers that sit fully to the left of the current edge (gap or abut).
        if (clip.timelineEnd() <= currentStart)
            minStart = qMax(minStart, clip.timelineEnd());
    }
    return qMax(start, minStart);
}

TimeUs clampClipEndNoOverlap(const Track &track, const QSet<QString> &excludeIds, TimeUs currentEnd,
                             TimeUs desiredEnd)
{
    TimeUs end = qMax(currentEnd, desiredEnd);
    for (const Clip &clip : track.clips) {
        if (excludeIds.contains(clip.id))
            continue;
        // Only blockers that sit fully to the right of the current edge (gap or abut).
        if (clip.timelineStart < currentEnd)
            continue;
        if (end > clip.timelineStart)
            end = clip.timelineStart;
    }
    return end;
}

TrackType trackTypeForClipType(ClipType type)
{
    switch (type) {
    case ClipType::Audio:
        return TrackType::Audio;
    case ClipType::Text:
        return TrackType::Text;
    case ClipType::Subtitle:
        return TrackType::Subtitle;
    case ClipType::Image:
    case ClipType::Shape:
        return TrackType::Shape;
    case ClipType::Video:
        break;
    }
    return TrackType::Video;
}

int defaultTrackForClipType(const Project &project, ClipType type)
{
    const TrackType trackType = trackTypeForClipType(type);
    const QList<Track> &tracks = project.tracks();
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks[i].type == trackType && tracks[i].allowsClipType(type))
            return i;
    }
    return -1;
}

int ensureTrackForClipType(Project &project, ClipType type, bool insertAtTop)
{
    const int existing = defaultTrackForClipType(project, type);
    if (existing >= 0)
        return existing;

    const Track track{.type = trackTypeForClipType(type)};
    if (insertAtTop)
        project.tracks().prepend(track);
    else
        project.tracks().append(track);
    return insertAtTop ? 0 : project.tracks().size() - 1;
}

int insertTrackAtTopForClipType(Project &project, ClipType type)
{
    project.tracks().prepend(Track{.type = trackTypeForClipType(type)});
    return 0;
}

int insertTrackAboveForClipType(Project &project, int trackIndex, ClipType type)
{
    const int at = qBound(0, trackIndex, project.tracks().size());
    project.tracks().insert(at, Track{.type = trackTypeForClipType(type)});
    return at;
}

TimeUs clipDurationForAsset(const MediaAsset *asset)
{
    if (!asset)
        return kImageClipDurationUs;

    if (asset->kind == MediaKind::Image)
        return kImageClipDurationUs;

    if (asset->durationUs > 0)
        return asset->durationUs;

    return kImageClipDurationUs;
}

TimeUs sourceDurationForClip(const Project &project, const Clip &clip)
{
    if (!clip.assetId.isEmpty()) {
        if (const MediaAsset *asset = project.asset(clip.assetId)) {
            if (asset->durationUs > 0)
                return asset->durationUs;
        }
    }

    if (clip.type == ClipType::Image || clip.type == ClipType::Shape)
        return kImageClipDurationUs;

    return qMax(clip.srcOut, clip.timelineDuration);
}

bool splitClipAtOffset(Clip &head, Clip &tail, TimeUs offset)
{
    if (offset < kMinClipDurationUs || head.timelineDuration - offset < kMinClipDurationUs)
        return false;

    const TimeUs sourceSpan = head.srcOut - head.srcIn;
    const TimeUs sourceOffset =
        head.hasSpeedCurve() ? head.speedCurve.sourceOffsetForTimelineOffset(offset, sourceSpan)
                             : head.sourceDeltaForTimelineDelta(offset);
    if (sourceOffset <= 0 || sourceOffset >= sourceSpan)
        return false;

    tail = head;
    tail.timelineStart = head.timelineStart + offset;
    tail.timelineDuration = head.timelineDuration - offset;

    // Curve positions are normalised over the clip's own source range, so each half needs the
    // parent's ramp resampled onto its shorter range — copying it verbatim would stretch both
    // halves back over the full shape and change how they play.
    if (head.hasSpeedCurve()) {
        const double cut = static_cast<double>(sourceOffset) / sourceSpan;
        const SpeedCurve parent = head.speedCurve;
        head.speedCurve = parent.subRange(0.0, cut);
        tail.speedCurve = parent.subRange(cut, 1.0);
    }

    if (head.reverse) {
        const TimeUs sourceAtSplit = head.srcOut - sourceOffset;
        tail.srcIn = head.srcIn;
        tail.srcOut = sourceAtSplit;
        head.srcIn = sourceAtSplit;
    } else {
        tail.srcIn = head.srcIn + sourceOffset;
        head.srcOut = head.srcIn + sourceOffset;
    }

    // Cue times are relative to the parent clip's timeline start, so the tail's copies have to be
    // rebased onto its new start; a cue straddling the cut is truncated on the left and resumes on
    // the right.
    if (!head.subtitleCues.isEmpty()) {
        QList<SubtitleCue> headCues;
        QList<SubtitleCue> tailCues;
        for (const SubtitleCue &cue : head.subtitleCues) {
            if (cue.startUs < offset) {
                SubtitleCue left = cue;
                left.endUs = qMin(cue.endUs, offset);
                if (left.endUs > left.startUs)
                    headCues.append(left);
            }
            if (cue.endUs > offset) {
                SubtitleCue right = cue;
                right.startUs = qMax<TimeUs>(cue.startUs - offset, 0);
                right.endUs = cue.endUs - offset;
                if (right.endUs > right.startUs)
                    tailCues.append(right);
            }
        }
        head.subtitleCues = headCues;
        tail.subtitleCues = tailCues;
        if (head.type == ClipType::Subtitle) {
            head.name = subtitleClipName(head.subtitleCues);
            tail.name = subtitleClipName(tail.subtitleCues);
        }
    }

    head.timelineDuration = offset;
    return true;
}

bool clipsCanMerge(const Clip &left, const Clip &right)
{
    if (left.type != right.type)
        return false;
    if (left.assetId.isEmpty() || left.assetId != right.assetId)
        return false;
    if (left.path != right.path)
        return false;
    if (left.reverse != right.reverse)
        return false;
    if (!qFuzzyCompare(left.speed, right.speed))
        return false;
    // Two ramps do not concatenate into one: the merged clip would have to carry both shapes
    // over a single normalised range.
    if (left.hasSpeedCurve() || right.hasSpeedCurve())
        return false;
    if (left.timelineEnd() != right.timelineStart)
        return false;

    if (left.reverse)
        return left.srcIn == right.srcOut;
    return left.srcOut == right.srcIn;
}

Clip mergeClips(const Clip &left, const Clip &right)
{
    Clip out = left;
    out.timelineDuration = left.timelineDuration + right.timelineDuration;
    if (left.reverse) {
        out.srcIn = right.srcIn;
        out.srcOut = left.srcOut;
    } else {
        out.srcOut = right.srcOut;
    }
    out.fadeOutUs = right.fadeOutUs;
    return out;
}

QList<ClipRef> linkedPartners(const Project &project, const Clip &clip)
{
    QList<ClipRef> out;
    if (clip.linkId.isEmpty())
        return out;

    for (int trackIndex = 0; trackIndex < project.tracks().size(); ++trackIndex) {
        const Track &track = project.tracks().at(trackIndex);
        for (int clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            const Clip &candidate = track.clips.at(clipIndex);
            if (candidate.id == clip.id)
                continue;
            if (candidate.linkId == clip.linkId)
                out.append(ClipRef{trackIndex, clipIndex});
        }
    }
    return out;
}

void syncLinkedTiming(Clip &dst, const Clip &src)
{
    dst.timelineStart = src.timelineStart;
    dst.timelineDuration = src.timelineDuration;
    dst.srcIn = src.srcIn;
    dst.srcOut = src.srcOut;
    dst.speed = src.speed;
    dst.speedCurve = src.speedCurve;
    dst.reverse = src.reverse;
    dst.fadeInUs = src.fadeInUs;
    dst.fadeOutUs = src.fadeOutUs;
    dst.fadeCurve = src.fadeCurve;
    dst.fadeShape = src.fadeShape;
}

QString assignSplitLinkIds(Clip &head, Clip &tail)
{
    if (head.linkId.isEmpty())
        return {};

    const QString tailLink = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tail.linkId = tailLink;
    if (head.suppressEmbeddedAudio)
        tail.suppressEmbeddedAudio = true;
    return tailLink;
}

namespace {

// Writes `value` as the sole keyframe of an empty track, leaving tracks that
// already carry explicit values (including animation) untouched.
void bakeIfImplicit(KeyframeTrack<double> &track, double value)
{
    if (track.isEmpty())
        track.setKeyframe(0, value);
}

void shiftTrackValues(KeyframeTrack<double> &track, double delta, double implicitValue)
{
    if (qFuzzyIsNull(delta))
        return;
    if (track.isEmpty()) {
        track.setKeyframe(0, implicitValue - delta);
        return;
    }
    KeyframeTrack<double> shifted;
    // Tangents ride along with each key now, so the whole shape survives the shift; only the
    // values move. dy is a delta in value units and is therefore unaffected by the offset.
    const QMap<TimeUs, Keyframe<double>> &values = track.keyframes();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        Keyframe<double> key = it.value();
        key.value -= delta;
        shifted.setKeyframe(it.key(), key);
    }
    track = shifted;
}

} // namespace

void rebaseClipLayout(Project &project, int oldWidth, int oldHeight, double originX, double originY)
{
    for (Track &track : project.tracks()) {
        if (track.type == TrackType::Audio)
            continue;
        for (Clip &clip : track.clips) {
            if (clip.type == ClipType::Audio)
                continue;
            bakeIfImplicit(clip.transformW, oldWidth);
            bakeIfImplicit(clip.transformH, oldHeight);
            shiftTrackValues(clip.transformX, originX, 0.0);
            shiftTrackValues(clip.transformY, originY, 0.0);
        }
    }
}

} // namespace drift

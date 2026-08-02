#pragma once

#include "Project.h"
#include "Time.h"

namespace drift {

constexpr TimeUs kImageClipDurationUs = 5 * kUsPerSecond;
constexpr TimeUs kTextClipDurationUs = 5 * kUsPerSecond;
constexpr TimeUs kSubtitleClipDurationUs = 30 * kUsPerSecond;
constexpr TimeUs kMinClipDurationUs = kUsPerSecond / 10;
constexpr TimeUs kSnapThresholdUs = 150'000;

// `extraTargets` are additional snap positions supplied by the caller — currently the
// detected beat grid, which is analysis state rather than something the project stores.
// Core never learns what a beat is; it just snaps to whatever times it is handed.
TimeUs snapTime(const Project &project, TimeUs time, bool snapEnabled, TimeUs playheadUs,
                const QList<TimeUs> &extraTargets = {});

TimeUs resolveClipStart(const Project &project, const Track &track, int excludeClipIndex,
                        TimeUs desiredStart, TimeUs duration, bool snapEnabled, TimeUs playheadUs,
                        const QList<TimeUs> &extraTargets = {});

TrackType trackTypeForClipType(ClipType type);

int defaultTrackForClipType(const Project &project, ClipType type);

int ensureTrackForClipType(Project &project, ClipType type, bool insertAtTop = false);

// Always prepends a fresh track (multiple tracks of the same type are allowed).
int insertTrackAtTopForClipType(Project &project, ClipType type);

// Inserts a fresh track directly above `trackIndex` (index 0 is the topmost track), pushing that
// track and everything below it down one. Returns the new track's index.
int insertTrackAboveForClipType(Project &project, int trackIndex, ClipType type);

TimeUs clipDurationForAsset(const MediaAsset *asset);

TimeUs sourceDurationForClip(const Project &project, const Clip &clip);

// Split `head` at `offset` from its timeline start into head + tail (same reverse/speed).
// Caller assigns `tail.id`. Returns false if the offset is too close to either end.
bool splitClipAtOffset(Clip &head, Clip &tail, TimeUs offset);

// True when `left` ends where `right` begins, shares media + reverse/speed, and source ranges abut.
bool clipsCanMerge(const Clip &left, const Clip &right);

// Merge abutting clips. Keeps left transforms/effects; takes right's fade-out.
Clip mergeClips(const Clip &left, const Clip &right);

struct ClipRef
{
    int trackIndex = -1;
    int clipIndex = -1;
};

// All clips sharing `clip.linkId` (excluding `clip` itself).
QList<ClipRef> linkedPartners(const Project &project, const Clip &clip);

// Mirror timeline/source timing from a linked source clip.
void syncLinkedTiming(Clip &dst, const Clip &src);

// Copy link fields when splitting a linked clip so both halves stay paired.
// Returns the link id assigned to `tail` (empty when `head` was not linked).
QString assignSplitLinkIds(Clip &head, Clip &tail);

// Prepares every clip for a canvas resize from `oldWidth`x`oldHeight`.
//
// Clips with no transformW/transformH keyframes fall back to the project size at
// composite time, so they would silently rescale themselves to the new canvas.
// This freezes that implicit size into explicit values first, then shifts stored
// positions by (-originX, -originY) so the region the user framed stays put.
// Clips are left visually stationary; whatever falls outside the new canvas is
// simply clipped away by the compositor.
void rebaseClipLayout(Project &project, int oldWidth, int oldHeight, double originX, double originY);

} // namespace drift

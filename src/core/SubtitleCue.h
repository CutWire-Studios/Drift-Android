#pragma once

#include "Time.h"

#include <QList>
#include <QString>

namespace drift {

struct SubtitleCue
{
    TimeUs startUs = 0; // relative to the parent clip's timeline start
    TimeUs endUs = 0;
    QString text;
};

const SubtitleCue *activeSubtitleCueAt(const QList<SubtitleCue> &cues, TimeUs localUs);

// Index of the word being spoken at localUs inside a [startUs, endUs) window, or -1 when the
// window is empty or has not started. Drives the Karaoke accent rule. The timings are the same
// proportional ones packSubtitleCues uses — true word timestamps aren't available from our ONNX
// Whisper export — so this is exact at cue boundaries and interpolated in between.
int activeWordIndexAt(const QString &text, TimeUs startUs, TimeUs endUs, TimeUs localUs);
int subtitleCueIndexAt(const QList<SubtitleCue> &cues, TimeUs localUs);
void sortSubtitleCues(QList<SubtitleCue> &cues);

// Timeline label for a subtitle clip, e.g. "Subtitles (12)".
QString subtitleClipName(const QList<SubtitleCue> &cues);

// Re-pack Whisper (or other) segment cues into shorter display lines, matching openai-whisper's
// VTT writer with word_timestamps + max_line_width / max_line_count. Words inside a segment get
// proportional timing (true attention-based word timestamps aren't available from our ONNX
// export); packing then walks those words and starts a new cue when the line would exceed
// maxLineWidth or when maxLineCount lines are already filled. A pause longer than 3s also
// forces a cue break.
QList<SubtitleCue> packSubtitleCues(const QList<SubtitleCue> &cues, int maxLineWidth = 42,
                                    int maxLineCount = 1);

} // namespace drift

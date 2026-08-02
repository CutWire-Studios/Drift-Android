#pragma once

#include "SubtitleCue.h"

#include <QList>
#include <QString>

namespace drift {

// Parse SubRip (.srt) text into clip-local cues. Accepts HH:MM:SS,mmm or HH:MM:SS.mmm
// timestamps, UTF-8 (with or without BOM), and multi-line cue bodies. Invalid or empty
// input returns false and leaves *outCues untouched when provided.
bool parseSrt(const QString &content, QList<SubtitleCue> *outCues, QString *error = nullptr);
bool parseSrtFile(const QString &path, QList<SubtitleCue> *outCues, QString *error = nullptr);

// Serialize cues to SubRip text (UTF-8, comma milliseconds, blank line between cues).
QString writeSrt(const QList<SubtitleCue> &cues);
bool writeSrtFile(const QString &path, const QList<SubtitleCue> &cues, QString *error = nullptr);

} // namespace drift

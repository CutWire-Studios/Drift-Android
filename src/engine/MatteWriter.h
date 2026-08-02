#pragma once

#include "core/Time.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

namespace drift {

// Writes a grayscale matte video: one lossless frame per source frame, consumed later as an
// ordinary video by ClipReaderPool.
//
// Lossless H.264 (qp=0) in MP4, with the mask in the luma plane and flat chroma. FFV1 in MKV is
// the more natural choice for a lossless matte and was tried first, but ClipReader cannot seek it
// reliably — past roughly the seventh frame it either repeats a frame or returns nothing, and an
// FFV1 file produced by ffmpeg itself behaves the same way. Do not "restore" FFV1 here without
// fixing that first. Luma is lossless at qp=0, so the mask edge survives intact; chroma is
// constant and unused.
class MatteWriter
{
public:
    MatteWriter();
    ~MatteWriter();

    // fps must match the source clip's frame rate: the matte is indexed by source time, so a
    // mismatch would slide the mask off the subject.
    bool open(const QString &path, const QSize &size, int fpsNum, int fpsDen, QString *errorOut);

    // Frames must be appended in order. Non-Grayscale8 input is converted.
    bool writeFrame(const QImage &mask, QString *errorOut);

    // Flushes and moves the temp file into place. Without this the file stays a ".part".
    bool finish(QString *errorOut);

    // Closes and removes the partial file. Safe to call after finish().
    void abort();

    MatteWriter(const MatteWriter &) = delete;
    MatteWriter &operator=(const MatteWriter &) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// <AppDataLocation>/mattes, created on demand.
QString matteCacheDir();

// A fresh, unused absolute path inside matteCacheDir().
QString newMattePath();

} // namespace drift

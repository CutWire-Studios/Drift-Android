#pragma once

#include "core/Clip.h"
#include "core/Time.h"

#include <QImage>
#include <QRectF>

// Text rasterization for the compositor. Glyphs are drawn on the CPU with QPainter and handed to
// the GPU as a layer texture, exactly like a decoded video frame.

struct TextRasterResult
{
    QImage image; // includes the bleed margin, so stroke/shadow/box are never cropped
    QRectF rect;  // destination rect in canvas px: the layout rect grown by that same bleed
};

// Rasterize (or return a cached copy of) the styled text. layoutRect is the clip's layout rect in
// canvas pixels; renderScale maps project pixels to canvas pixels. activeWordIndex is the word the
// playhead is currently on, and only matters for the Karaoke accent rule (-1 = none).
TextRasterResult rasterizeText(const drift::Clip &clip, const QString &text, const QRectF &layoutRect,
                               double renderScale, int activeWordIndex = -1);
TextRasterResult rasterizeText(const drift::Clip &clip, const QRectF &layoutRect, double renderScale,
                               int activeWordIndex = -1);

// One reveal span (a character, word or line) of a text clip, rasterized on its own so the
// compositor can stagger the entrance/exit across the block. Each span is a self-contained texture
// with its own destination rect; motion still rides on the layer, so the textures stay cacheable.
struct TextSpanRaster
{
    QImage image;
    QRectF rect;    // destination rect in canvas px (bleed already included)
    int index = 0;  // reading-order index; -1 marks the static box background
    int count = 0;  // number of glyph spans (excludes the box)
};

// Split the styled text into per-`unit` spans (Word / Character / Line) and rasterize each. Returns
// empty for TextAnimUnit::Block — callers use rasterizeText for the whole-layer path. The optional
// box background is returned first with index == -1.
QList<TextSpanRaster> rasterizeTextSpans(const drift::Clip &clip, const QString &text,
                                         const QRectF &layoutRect, double renderScale,
                                         drift::TextAnimUnit unit, int activeWordIndex = -1);

// Entrance/exit motion, sampled at a timeline instant. Applied to the *layer* — never to the
// raster — so the cached texture stays valid for every frame of the animation.
struct TextAnimSample
{
    double opacity = 1.0;
    double dx = 0.0;
    double dy = 0.0;
    double scale = 1.0;
    double blurPx = 0.0;
};

TextAnimSample sampleTextAnimation(const drift::Clip &clip, drift::TimeUs timelineUs,
                                   const QRectF &layoutRect, double renderScale);

// Entrance/exit motion for a single reveal span, staggered by the animation's unit/stagger/order.
// spanIndex is the span's reading-order index and spanCount the total glyph-span count.
TextAnimSample sampleTextSpanAnimation(const drift::Clip &clip, drift::TimeUs timelineUs,
                                       int spanIndex, int spanCount, const QRectF &layoutRect,
                                       double renderScale);

// Same, but scoped to a single subtitle cue's [start, end) window so each cue on a subtitle
// clip plays its own entrance and exit, one after another.
TextAnimSample sampleSubtitleCueAnimation(const drift::Clip &clip, const drift::SubtitleCue &cue,
                                          drift::TimeUs timelineUs, const QRectF &layoutRect,
                                          double renderScale);

// Widest blur an entrance/exit can ask for, in project px. Reserved in the bleed margin up front so
// the image size — and therefore the cache key — does not change as the animation plays.
constexpr double kTextBlurMaxPx = 24.0;

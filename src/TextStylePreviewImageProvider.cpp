#include "TextStylePreviewImageProvider.h"

#include "core/Clip.h"
#include "core/TextStyle.h"
#include "engine/TextRaster.h"

#include <QPainter>

namespace {

// Fallback when a pack has no sampleText (should not happen for built-in packs).
const QString kFallbackSample = QStringLiteral("Your text here");

// Preview cards are sized in project pixels against a 1080-wide canvas, which is what maps a pack's
// pixelSize onto the card at the same relative scale the compositor uses.
constexpr double kReferenceWidth = 1080.0;
constexpr int kDefaultWidth = 240;
constexpr int kDefaultHeight = 108;

} // namespace

TextStylePreviewImageProvider::TextStylePreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage TextStylePreviewImageProvider::requestImage(const QString &id, QSize *size,
                                                   const QSize &requestedSize)
{
    const drift::TextPreset *preset = drift::textPresetForId(id);
    if (!preset) {
        if (size)
            *size = QSize();
        return {};
    }

    const int width = requestedSize.width() > 0 ? requestedSize.width() : kDefaultWidth;
    const int height = requestedSize.height() > 0 ? requestedSize.height() : kDefaultHeight;

    drift::Clip clip;
    clip.type = drift::ClipType::Text;
    clip.textStyle = preset->style;

    const QString sample = preset->sampleText.isEmpty() ? kFallbackSample : preset->sampleText;

    const QRectF layoutRect(0, 0, width, height);
    // A karaoke pack accents nothing at all without a playhead, so the card borrows the second word.
    const int activeWord = preset->style.accent.rule == drift::WordAccentRule::Karaoke ? 1 : -1;
    const TextRasterResult raster =
        rasterizeText(clip, sample, layoutRect, width / kReferenceWidth, activeWord);

    QImage card(width, height, QImage::Format_ARGB32_Premultiplied);
    card.fill(Qt::transparent);
    if (!raster.image.isNull()) {
        QPainter p(&card);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(raster.rect.topLeft(), raster.image);
    }

    if (size)
        *size = card.size();
    return card;
}
